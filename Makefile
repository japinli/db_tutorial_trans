db: db.c
	gcc db.c -o db

.PHONY: clean
clean:
	rm -rf db
