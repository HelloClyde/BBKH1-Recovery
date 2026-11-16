/* Unicode entry point for the unmodified upstream libwdi example.
   libwdi consumes UTF-8 paths; Windows narrow argv uses the system ANSI codepage.
   The diagnostic option below only checks conversion; it calls no libwdi API. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
int wdi_cli_main_utf8(int,char **);
int wmain(int argc,wchar_t **wide) {
    char **argv=calloc((size_t)argc+1,sizeof(*argv));if(!argv)return 1;
    for(int i=0;i<argc;i++) {
        int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,NULL,0,NULL,NULL);
        if(!bytes)return 1;
        argv[i]=malloc((size_t)bytes);if(!argv[i])return 1;
        if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,argv[i],bytes,NULL,NULL))return 1;
    }
    SetConsoleOutputCP(CP_UTF8);
    int result;
    if(argc==3&&!wcscmp(wide[1],L"--h1-check-utf8")){puts(argv[2]);result=0;}
    else result=wdi_cli_main_utf8(argc,argv);
    for(int i=0;i<argc;i++)free(argv[i]);
    free(argv);return result;
}
