#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <bio.h>
#include <draw.h>
#include <geometry.h>
#include <graphics.h>

#define NHASH		(1<<5)
#define HASHMASK	(NHASH-1)
#define	BGVLONG(p)	((vlong)(p)[0]|((vlong)(p)[1]<<8)|((vlong)(p)[2]<<16)|((vlong)(p)[3]<<24)|((vlong)(p)[4]<<32)|((vlong)(p)[5]<<40)|((vlong)(p)[6]<<48)|((vlong)(p)[7]<<56))

inline double
BGDOUBLE(uchar *buf)
{
	union {
		vlong bits;
		double d;
	} val;

	val.bits = BGVLONG(buf);
	return val.d;
}

typedef struct Memverts Memverts;

typedef struct Render Render;
typedef struct Client Client;
typedef struct RVerts RVerts;

typedef struct Cmd Cmd;

struct Memverts
{
	int		maxn;
	int		n;
	Vertex*	data;
};

enum Rndmode
{
	Points	= 0,
	Lines	= 1,
	Wire	= 2,
	Tris	= 3,
};

struct Client
{
	int		clientid;
	int		drawfd;
	RVerts*	rverts[NHASH];
	Matrix3	xfmat;
};

struct RVerts
{
	int			id;
	Memverts*	verts;
	RVerts*		next;
};

struct Cmd
{
	int n;
	char *(*handler)(Client*, uchar*, int);
};

RVerts* renderlookup(Client*, int);
RVerts* allocrverts(Memverts*);
void renderfreeverts(RVerts*);
void freememverts(Memverts*);
void usage(void);
void fsread(Req*);
void fswrite(Req*);
char *cmdalloc(Client*, uchar*, int);
char *cmdbind(Client*, uchar*, int);
char *cmdfree(Client*, uchar*, int);
char *cmdwrite(Client*, uchar*, int);
char *cmdsetxfmat(Client*, uchar*, int);
char *cmdraster(Client*, uchar*, int);
void threadmain(int, char *[]);

static char Enodrawbound[]	= "no draw bound";
static char Enobuf[]		= "unknown id for render buffer";
static char Enoctlreq[]     = "unknown render control request";
static char Eshortmsg[]		= "short render message";
static char Eshortread[]	= "render read too short";
static char Ebufexists[]	= "buffer id in use";
static char Erendermem[]	= "buffer memory allocation failed";

Cmd cmds[128] = {
	['a'] = { 4+4,						cmdalloc },
	['B'] = { 4,						cmdbind },
	['f'] = { 4,						cmdfree },
	['w'] = { 4+4,						cmdwrite },
	['M'] = { 4*4*8,					cmdsetxfmat },
	['R'] = { 4+4+4*8+4*8+1+4+2*4+4,	cmdraster },
};

Srv fs = {
	.read	= fsread,
	.write	= fswrite,
};

Tree *tree;
int logfd;
File *newf;

RVerts*
renderlookup(Client *client, int id)
{
	RVerts *v;

	v = client->rverts[id & HASHMASK];
	while(v){
		if(v->id == id)
			return v;
		v = v->next;
	}
	return nil;
}

void
renderfreerverts(RVerts *v)
{
	freememverts(v->verts);
	free(v);
}

RVerts*
allocrverts(Memverts *memv)
{
	RVerts *v;

	v = malloc(sizeof(RVerts));
	if(!v)
		return nil;
	v->verts = memv;
	return v;
}

void
freememverts(Memverts *memv)
{
	free(memv->data);
	free(memv);
}

void
usage(void)
{
	fprint(2, "usage: %s [-m mountpoint]\n", argv0);
	exits("usage");
}

void
fsread(Req *r)
{
	static int n = 1;
	File *f, *connf;
	Client *client;
	char conn[12];

	f = r->fid->file;

	if(f == newf){
		client = malloc(sizeof(Client));
		memset(client, 0, sizeof(Client));
		client->clientid = n++;
		snprint(conn, 12, "%d", n);
		connf = createfile(tree->root, conn, nil, DMDIR|0555, nil);
		connf->aux = (void*)client;
		createfile(connf, "data", nil, 0777, nil);
		snprint(conn, 12, "%011d", n++);
		readstr(r, conn);
		respond(r, nil);
		return;
	}

	respond(r, nil);
}

void
fswrite(Req *r)
{
	File *f;
	Client *client;
	Cmd cmd;
	int count;
	char c;
	uchar *data;

	f = r->fid->file;
	count = r->ifcall.count;
	data = (uchar*)r->ifcall.data;

	if(count == 0){
		respond(r, nil);
		return;
	}else if(f->parent){
		client = f->parent->aux;
		c = *data++;
		cmd = cmds[c];
		if(cmd.handler == nil){
			respond(r, Enoctlreq);
			return;
		}
		if(count-1 < cmd.n){
			respond(r, Eshortmsg);
			return;
		}
		respond(r, cmd.handler(client, data, count-1));
		return;
	}

	respond(r, nil);
}

char*
cmdalloc(Client *client, uchar *b, int count)
{
	Memverts *memv;
	RVerts *v;
	int id, maxn;

	USED(count);

	id = BGLONG(b);
	maxn = BGLONG(b+4);
	memv = malloc(sizeof(Memverts));
	if(!memv)
		return Erendermem;
	memv->maxn = maxn;
	memv->n = 0;
	memv->data = malloc(maxn * sizeof(Vertex));
	if(!memv->data) {
		free(memv);
		return Erendermem;
	}
	v = allocrverts(memv);
	if(!v) {
		freememverts(memv);
		return Erendermem;
	}
	v->id = id;
	v->next = client->rverts[id & HASHMASK];
	client->rverts[id & HASHMASK] = v;

	return nil;
}

char*
cmdbind(Client *client, uchar *b, int count)
{
	char dir[128];
	int dirno, drawfd;

	USED(count);

	dirno = BGLONG(b);
	sprint(dir, "/dev/draw/%d/data", dirno);
	if((drawfd = open(dir, ORDWR)) < 0)
		return "no draw found";
	client->drawfd = drawfd;

	return nil;
}

char*
cmdfree(Client *client, uchar *b, int count)
{
	int id;

	USED(client);
	USED(count);

	id = BGLONG(b);
	/* TODO */

	USED(id);
	return nil;
}

char*
cmdwrite(Client *client, uchar *b, int count)
{
	RVerts *v;
	Memverts *memv;
	int id, n;

	id = BGLONG(b);
	n = BGLONG(b+4);
	v = renderlookup(client, id);
	if(!v)
		return Enobuf;
	if(count < 4+4 + n*sizeof(Vertex))
		return Eshortmsg;
	memv = v->verts;
	if(memv->maxn < n)
		return "too big";
	memv->n = n;
	memcpy(memv->data, b+8, n * sizeof(Vertex));

	return nil;
}

char*
cmdsetxfmat(Client *client, uchar *b, int count)
{
	memcpy(client->xfmat, b, sizeof(Matrix3));
	fprint(logfd, "setxfmat\n");

	USED(count);

	return nil;
}

char*
rasterlines(Client *client, int dstid, int n, Vertex *v, int thick, Point sp, int srcid)
{
	Point3 p0, p1;
	Point q0, q1;
	uchar rpc[1+4+2*4+2*4+4+4+4+4+2*4];
	int i;

	rpc[0] = 'L';
	BPLONG(rpc+1, dstid);
	BPLONG(rpc+21, 0);
	BPLONG(rpc+25, 0);
	BPLONG(rpc+29, thick);
	BPLONG(rpc+33, srcid);
	memcpy(rpc+37, &sp, sizeof(Point));

	for(i = 0; i < n>>1; i++) {
		p0 = xform3(v[2*i].p, client->xfmat);
		p1 = xform3(v[2*i + 1].p, client->xfmat);
		q0 = Pt(p0.x, p0.y);
		q1 = Pt(p1.x, p1.y);
		memcpy(rpc+5, &q0, sizeof(Point));
		memcpy(rpc+13, &q1, sizeof(Point));
		write(client->drawfd, rpc, sizeof rpc);
	}

	return nil;
}

char*
cmdraster(Client *client, uchar *b, int count)
{
	Point3 pos;
	Point sp;
	Quaternion rot;
	RVerts *v;
	Memverts *memv;
	int dstid, vertid, mode, thick, srcid;

	USED(count);

	dstid = BGLONG(b);
	vertid = BGLONG(b+4);
	memcpy(&pos, b+8, sizeof(Point3));
	memcpy(&rot, b+40, sizeof(Quaternion));
	mode = b[72];
	thick = BGLONG(b+73);
	memcpy(&sp, b+77, sizeof(Point));
	srcid = BGLONG(b+85);
	v = renderlookup(client, vertid);
	if(!v)
		return Enobuf;
	memv = v->verts;
	switch(mode){
	case Lines:
		rasterlines(client, dstid, memv->n, memv->data, thick, sp, srcid);
		return nil;
	default:
		return "mode not implemented";
	}
}

void
threadmain(int argc, char *argv[])
{
	char *mtpt;

	mtpt = "/mnt/render";
	ARGBEGIN{
	case 'm':
		mtpt = EARGF(usage());
	default:
		usage();
	}ARGEND

	if(argc != 0)
		usage();
	if((logfd = create("/sys/log/render", OWRITE, 0664)) < 0)
		sysfatal("open: %r");

	tree = alloctree(nil, nil, DMDIR|0777, nil);
	newf = createfile(tree->root, "new", nil, 0600, nil);
	fs.tree = tree;

	threadpostmountsrv(&fs, nil, mtpt, MREPL|MCREATE);
}
