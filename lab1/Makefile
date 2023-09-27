all: catpng findpng

catpng: catpng.c
	gcc catpng.h catpng.c ./starter/png_util/zutil.c ./starter/png_util/crc.c -o catpng -lz 

findpng: findpng.c
	gcc findpng.c pnginfo.c -o findpng

clean:
	rm findpng catpng