#ifndef VERSION_H
#define VERSION_H

#define APPDOMAIN "org.hhmi.janelia"
#define APPNAME   "FG_Test"
#define APPNAME_SHORT "FG_Test"
#define VERSION 0.1

#define STR1(x) #x
#define STR(x) STR1(x)
#define VERSION_STR STR(VERSION)
#define APPNAME_FULL (APPNAME " v" VERSION_STR)

#endif // VERSION_H
