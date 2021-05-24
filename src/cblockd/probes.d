provider cblockd {
	probe cblock_create(char []);
	probe cblock_destroy(char [], int);
	probe cblock_cleanup(char [], int);
};
