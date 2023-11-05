all: paster

paster: paster2.c
	gcc paster2.c shm_stack.c ../lab1/starter/png_util/zutil.c ../lab1/starter/png_util/crc.c -o paster2 -lcurl -pthread -lz
	
clean:
	rm paster