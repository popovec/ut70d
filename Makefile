all:	ut70d

ut70d:	ut70d.c
	cc -Wall ut70d.c -o ut70d -g


clean:
	rm -f ut70d
	rm -f *~
