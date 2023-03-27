#pragma once
class Server
{
public:
	Server(int port,const char* workPath);

	void epollRun();

private:
	int lfd;			//listen fd
	int epfd;			//epoll tree root
	
	int port;			//server port

	int initListenFd();
	void acceptClient();
	void disconnect(int cfd);

	void dealRead(int cfd);
	void httpRequest(const char* request, int cfd);
	void sendDir(int cfd, const char* dirname);
	void sendFile(int cfd, const char* filename);
	void sendRespondHead(int cfd, int no, const char* desp, const char* type, long len);
	void sendError(int cfd, int status, char* title, char* text);

	int getLine(int sock, char* buf, int size);
	const char* getFileType(const char* name);
	void encodeStr(char* to, int tosize, const char* from);
	void decodeStr(char* to, char* from);
	int hex2dec(char c);
};

