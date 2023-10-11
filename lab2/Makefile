all: paster

paster: paster.c
	gcc paster.c ../lab1/starter/png_util/zutil.c ../lab1/starter/png_util/crc.c -o paster -lcurl -pthread -lz
	
clean:
	rm paster