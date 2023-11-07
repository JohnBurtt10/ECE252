all: findpng2

findpng2: findpng2.c
	gcc findpng2.c ../lab1/starter/png_util/zutil.c ../lab1/starter/png_util/crc.c -o findpng2 -lcurl -pthread -lz
	
clean:
	rm findpng2