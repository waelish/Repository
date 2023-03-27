#include "Server.h"

int main()
{
	//create and initialize Server
	Server* p = new Server(9999, "/home/freedom/work/network");

	//turn on Server
	p->epollRun();

	return 0;
}