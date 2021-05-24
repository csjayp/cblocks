cblockd::cblock_create
{
	printf("cblock created: %s\n", copyinstr(arg0));
}

cblockd::cblock_destroy
{
	printf("cblock destroyed: %s status=%d\n", copyinstr(arg0), arg1);
}

cblockd::cblock_cleanup
{
        printf("cblock cleaned up: %s status=%d\n", copyinstr(arg0), arg1);
}
