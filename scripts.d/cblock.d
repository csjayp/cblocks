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
        printf("cblock cleaned up: %s status=%d type=%s\n",
	    copyinstr(arg0), arg1, copyinstr(arg2));
}

cblockd::cblock_console_attach
{
	printf("console was attached to %s\n", copyinstr(arg0));
}

cblockd::cblock_console_detach
{
	printf("console was detached from %s\n", copyinstr(arg0));
}
