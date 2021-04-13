#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <geometry.h>
#include <graphics.h>
#include <render.h>

#define nelems(a)	(sizeof(a) / sizeof(*a))

Image *col;
int renderfd;

void
a(int id, int maxn)
{
	char rpc[1+4+4];

	rpc[0] = 'a';
	BPLONG(rpc+1, id);
	BPLONG(rpc+5, maxn);

	write(renderfd, rpc, sizeof rpc);
}

void
B(int dirno)
{
	char rpc[1+4];

	rpc[0] = 'B';
	BPLONG(rpc+1, dirno);

	write(renderfd, rpc, sizeof rpc);
}

void
f(int id)
{
	char rpc[1+4];

	rpc[0] = 'f';
	BPLONG(rpc+1, id);

	write(renderfd, rpc, sizeof rpc);
}

void
M(Matrix3 xform)
{
	char rpc[1+sizeof(Matrix3)];

	rpc[0] = 'M';
	memcpy(rpc+1, xform, sizeof(Matrix3));

	write(renderfd, rpc, sizeof rpc);
}

void
R(int dstid, int vertid, Point3 pos, Quaternion rot, char mode, int thick, Point sp, int srcid)
{
	char rpc[1+4+4+4*8+4*8+1+4+2*4+4];

	rpc[0] = 'R';
	BPLONG(rpc+1, dstid);
	BPLONG(rpc+5, vertid);
	memcpy(rpc+9, &pos, sizeof(Point3));
	memcpy(rpc+41, &rot, sizeof(Quaternion));
	rpc[73] = mode;
	BPLONG(rpc+74, thick);
	memcpy(rpc+78, &sp, sizeof(Point));
	BPLONG(rpc+86, srcid);

	write(renderfd, rpc, sizeof rpc);
}

void
w(int dstid, int n, Vertex *verts)
{
	char *rpc;

	rpc = malloc(1+4+4+n*sizeof(Vertex));
	rpc[0] = 'w';
	BPLONG(rpc+1, dstid);
	BPLONG(rpc+5, n);
	memcpy(rpc+9, verts, n*sizeof(Vertex));

	write(renderfd, rpc, 1+4+4+n*sizeof(Vertex));
	free(rpc);
}

void
resize(void)
{
	Rectangle r;

	if(getwindow(display, Refnone) < 0)
		sysfatal("getwindow: %r\n");

	Matrix3 tovpmat = {
		Dx(screen->r), 0, 0, screen->r.min.x,
		0, Dy(screen->r), 0, screen->r.min.y,
		0, 0, 1, 0,
		0, 0, 0, 1,
	};

	r.min = screen->r.min;
	r.max = addpt(screen->r.min, Pt(100, 100));
	draw(screen, screen->r, display->white, nil, ZP);
	M(tovpmat);
	flushimage(display, 1);

	R(screen->id, 1,
		(Point3){0, 0, 0, 0},
		(Quaternion){1, 0, 0, 0},
		Lines,
		0,
		ZP,
		col->id
	);

	flushimage(display, 1);
}

void
usage(void)
{
	fprint(2, "usage: %s\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	Mousectl *mctl;
	char buf[128];
	int dirno, newfd;
	Vertex verts[] = {
		{ .p = Pt3(0, 0, 0, 1) },
		{ .p = Pt3(1, 1, 0, 1) },
	};

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(initdraw(nil, nil, "try") < 0)
		sysfatal("initdraw: %r");
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((newfd = open("/mnt/render/new", OREAD)) < 0)
		sysfatal("open: %r");
	if(read(newfd, buf, 11) < 0)
		sysfatal("read: %r");

	buf[11] = '\0';
	dirno = atoi(buf);
	sprint(buf, "/mnt/render/%d/data", dirno);
	if((renderfd = open(buf, OWRITE)) < 0)
		sysfatal("open: %r");

	col = allocimage(display, Rect(0, 0, 1, 1), RGBA32, 1, 0xFF0000FF);
	flushimage(display, 0);
	B(display->dirno);
	a(1, nelems(verts));
	w(1, nelems(verts), verts);
	resize();

	for(;;){
		enum { MOUSE, RESIZE };
		Alt a[] = {
			{ mctl->c, &mctl->Mouse, CHANRCV },
			{ mctl->resizec, nil, CHANRCV },
			{ nil, nil, CHANNOBLK },
		};

		switch(alt(a)){
		case MOUSE: break;
		case RESIZE: resize(); break;
		}
	}
}
