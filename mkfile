</$objtype/mkfile
BIN=/$objtype/bin

TARG=renderfs
OFILES=\
	renderfs.$O\

HFILES=\
	renderfs.h\

LIB=\
	libgraphics/libgraphics.a$O\
	libgeometry/libgeometry.a$O\

CFLAGS=$CFLAGS -I. -Ilibgeometry -Ilibgraphics

</sys/src/cmd/mkone

libgeometry/libgeometry.a$O:
	cd libgeometry
	mk install

libgraphics/libgraphics.a$O:
	cd libgraphics
	mk install

clean nuke:V:
	rm -f *.[$OS] [$OS].out $TARG
	@{cd libgeometry; mk $target}
	@{cd libgraphics; mk $target}
