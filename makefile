.PHONY: read_edid
read_edid: main.o
	gcc $^ -o $@ -li2c

.PHONY: clean
clean:
	rm -rf *.o read_edid
