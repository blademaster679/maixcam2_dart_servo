#pragma once
#include "common.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cerrno>
#endif
class Serial{
#ifdef _WIN32
 HANDLE h=INVALID_HANDLE_VALUE;
#else
 int fd=-1;termios old{};bool have_old=false;
#endif
public:
 Serial(const std::string&port,int baud){
  require(baud==9600||baud==57600||baud==115200,"Supported baud: 9600,57600,115200");
  try{
#ifdef _WIN32
  std::string name=port.rfind("\\\\.\\",0)==0?port:"\\\\.\\"+port;h=CreateFileA(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);require(h!=INVALID_HANDLE_VALUE,"Cannot open serial port");DCB d{};d.DCBlength=sizeof(d);require(GetCommState(h,&d),"GetCommState failed");d.BaudRate=baud;d.ByteSize=8;d.Parity=NOPARITY;d.StopBits=ONESTOPBIT;d.fBinary=TRUE;d.fParity=FALSE;d.fOutxCtsFlow=FALSE;d.fOutxDsrFlow=FALSE;d.fDtrControl=DTR_CONTROL_DISABLE;d.fRtsControl=RTS_CONTROL_DISABLE;d.fDsrSensitivity=FALSE;d.fOutX=FALSE;d.fInX=FALSE;require(SetCommState(h,&d),"SetCommState failed");COMMTIMEOUTS t{};t.ReadIntervalTimeout=MAXDWORD;t.ReadTotalTimeoutConstant=100;require(SetCommTimeouts(h,&t),"SetCommTimeouts failed");
#else
  fd=::open(port.c_str(),O_RDWR|O_NOCTTY|O_NONBLOCK);require(fd>=0,"Cannot open serial port");termios t{};require(tcgetattr(fd,&old)==0,"tcgetattr failed");have_old=true;t=old;cfmakeraw(&t);speed_t speed=baud==9600?B9600:baud==57600?B57600:B115200;cfsetispeed(&t,speed);cfsetospeed(&t,speed);t.c_cflag=(t.c_cflag&~(CSIZE|PARENB|CSTOPB|CRTSCTS))|CS8|CLOCAL|CREAD;t.c_cc[VMIN]=0;t.c_cc[VTIME]=1;require(tcsetattr(fd,TCSANOW,&t)==0,"tcsetattr failed");
#endif
  }catch(...){close();throw;}
 }
 bool read(char&c){
#ifdef _WIN32
 DWORD n=0;require(ReadFile(h,&c,1,&n,nullptr),"Serial read failed");return n==1;
#else
 auto n=::read(fd,&c,1);if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)throw std::runtime_error("Serial read failed");if(n!=1)std::this_thread::sleep_for(std::chrono::milliseconds(2));return n==1;
#endif
 }
 void close(){
#ifdef _WIN32
 if(h!=INVALID_HANDLE_VALUE){CloseHandle(h);h=INVALID_HANDLE_VALUE;}
#else
 if(fd>=0){if(have_old)tcsetattr(fd,TCSANOW,&old);::close(fd);fd=-1;}
#endif
 }
 ~Serial(){close();}Serial(const Serial&)=delete;Serial&operator=(const Serial&)=delete;
};
