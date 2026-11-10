#pragma once

namespace commands
{
void helloArx();
void jsEval();
void jsExecute();
void jsRepl();
void jsServer();
void qtoProps();
void initIpcBridge();
void shutdownIpcBridge();
bool startJsIpcServer(int port = 8123);
bool isJsIpcServerRunning();
void stopJsIpcServer();
void setEntityColor();
}
