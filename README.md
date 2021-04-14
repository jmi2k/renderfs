# renderfs

9P server which exposes primitives for 3D rendering, backed by `draw(3)`.

## Disclaimer

It won't work, I'm almost sure of that. Congratulations if you can test it!

## Dependencies

Copy or bind `libgeometry` and `libgraphics` from
[rodri](http://git.antares-labs.eu/) to the empty folders with the same name in
this repo. Then:

```
mk
6.out
# renderfs is running at /mnt/render
```