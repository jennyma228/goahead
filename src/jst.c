/*
    jst.c -- JavaScript templates
  
    Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/

#include    "goahead.h"
#include    "js.h"

char contentip[32]="183.37.158.59";
#define TITLE_MAX 40
#define SUMMARY_MAX 100
#define FONT_size 10
struct sockaddr_in adr_inet; /* AF_INET */

#if ME_GOAHEAD_JAVASCRIPT
/********************************** Locals ************************************/

static WebsHash websJstFunctions = -1;  /* Symbol table of functions */

/***************************** Forward Declarations ***************************/

static char *strtokcmp(char *s1, char *s2);
static char *skipWhite(char *s);
static int strgetname(const char *s1, char *s2, int size);
static int _atoi(const char *s);
static int websGetPage( Webs *wp);
static int websGetQdata(Webs *wp, char *option);
#if USING_SQLITE
static int sqlGetList(int channel, int item);
static int sqlGetNextList(int current_lst, int flag);
static int sqlGetPicture(int list);
static int sqlGetChannel(int list);
static int sqlGetItem(int list);
static int sqlGetText(int list);
static int sqlGetTable(const char *table,  int id, const char *name, char * value, int size );
static int sqlGetMax(const char *table);
#endif

/************************************* Code ***********************************/
/*
    Process requests and expand all scripting commands. We read the entire web page into memory and then process. If
    you have really big documents, it is better to make them plain HTML files rather than Javascript web pages.
 */
static bool jstHandler(Webs *wp)
{
    WebsFileInfo    sbuf;
    char            *token, *lang, *result, *ep, *cp, *buf, *nextp, *last;
    ssize           len;
    int             rc, jid;

    assert(websValid(wp));
    assert(wp->filename && *wp->filename);
    assert(wp->ext && *wp->ext);

    buf = 0;
    if ((jid = jsOpenEngine(wp->vars, websJstFunctions)) < 0) {
        websError(wp, HTTP_CODE_INTERNAL_SERVER_ERROR, "Cannot create JavaScript engine");
        goto done;
    }
    jsSetUserHandle(jid, wp);

    if (websPageStat(wp, &sbuf) < 0) {
        websError(wp, HTTP_CODE_NOT_FOUND, "Cannot stat %s", wp->filename);
        goto done;
    }
    if (websPageOpen(wp, O_RDONLY | O_BINARY, 0666) < 0) {
        websError(wp, HTTP_CODE_NOT_FOUND, "Cannot open URL: %s", wp->filename);
        goto done;
    }
    /*
        Create a buffer to hold the web page in-memory
     */
    len = sbuf.size;
    if ((buf = walloc(len + 1)) == NULL) {
        websError(wp, HTTP_CODE_INTERNAL_SERVER_ERROR, "Cannot get memory");
        goto done;
    }
    buf[len] = '\0';

    if (websPageReadData(wp, buf, len) != len) {
        websError(wp, HTTP_CODE_NOT_FOUND, "Cant read %s", wp->filename);
        goto done;
    }
    websPageClose(wp);
    websWriteHeaders(wp, (ssize) -1, 0);
    websWriteHeader(wp, "Pragma", "no-cache");
    websWriteHeader(wp, "Cache-Control", "no-cache");
    websWriteEndHeaders(wp);

    /*
        Scan for the next "<%"
     */
    last = buf;
    for (rc = 0; rc == 0 && *last && ((nextp = strstr(last, "<%")) != NULL); ) {
        websWriteBlock(wp, last, (nextp - last));
        nextp = skipWhite(nextp + 2);
        /*
            Decode the language
         */
        token = "language";
        if ((lang = strtokcmp(nextp, token)) != NULL) {
            if ((cp = strtokcmp(lang, "=javascript")) != NULL) {
                /* Ignore */;
            } else {
                cp = nextp;
            }
            nextp = cp;
        }

        /*
            Find tailing bracket and then evaluate the script
         */
        if ((ep = strstr(nextp, "%>")) != NULL) {

            *ep = '\0';
            last = ep + 2;
            nextp = skipWhite(nextp);
            /*
                Handle backquoted newlines
             */
            for (cp = nextp; *cp; ) {
                if (*cp == '\\' && (cp[1] == '\r' || cp[1] == '\n')) {
                    *cp++ = ' ';
                    while (*cp == '\r' || *cp == '\n') {
                        *cp++ = ' ';
                    }
                } else {
                    cp++;
                }
            }
            if (*nextp) {
                result = NULL;

                if (jsEval(jid, nextp, &result) == 0) {
                    /*
                         On an error, discard all output accumulated so far and store the error in the result buffer. 
                         Be careful if the user has called websError() already.
                     */
                    rc = -1;
                    if (websValid(wp)) {
                        if (result) {
                            websWrite(wp, "<h2><b>Javascript Error: %s</b></h2>\n", result);
                            websWrite(wp, "<pre>%s</pre>", nextp);
                            wfree(result);
                        } else {
                            websWrite(wp, "<h2><b>Javascript Error</b></h2>\n%s\n", nextp);
                        }
                        websWrite(wp, "</body></html>\n");
                        rc = 0;
                    }
                    goto done;
                }
            }

        } else {
            websError(wp, HTTP_CODE_INTERNAL_SERVER_ERROR, "Unterminated script in %s: \n", wp->filename);
            goto done;
        }
    }
    /*
        Output any trailing HTML page text
     */
    if (last && *last && rc == 0) {
        websWriteBlock(wp, last, strlen(last));
    }

/*
    Common exit and cleanup
 */
done:
    if (websValid(wp)) {
        websPageClose(wp);
        if (jid >= 0) {
            jsCloseEngine(jid);
        }
    }
    websDone(wp);
    wfree(buf);
    return 1;
}


static void closeJst()
{
    if (websJstFunctions != -1) {
        hashFree(websJstFunctions);
        websJstFunctions = -1;
    }
}


PUBLIC int websJstOpen()
{
    websJstFunctions = hashCreate(WEBS_HASH_INIT * 2);
    websDefineJst("write", websJstWrite);
    websDefineJst("get_title", websJstGetTitle);
    websDefineJst("get_text", websJstGetSummary);
    websDefineJst("get_auth", websJstGetAuth);
    websDefineJst("get_time", websJstGetTime);
    websDefineJst("get_pic", websJstGetPicture);
    websDefineJst("get_ch", websJstGetChannel);
    websDefineJst("get_page", websJstGetPage);
    websDefineJst("get_show", websJstGetShow);
    websDefineJst("get_login", websJstGetLogin);
    websDefineJst("get_ip", websJstGetContentIP);
    websDefineJst("get_content", websJstGetContent);
    websDefineHandler("jst", 0, jstHandler, closeJst, 0);
    return 0;
}


/*
    Define a Javascript function. Bind an Javascript name to a C procedure.
 */
PUBLIC int websDefineJst(char *name, WebsJstProc fn)
{
    return jsSetGlobalFunctionDirect(websJstFunctions, name, (JsProc) fn);
}


/*
    Javascript write command. This implemements <% write("text"); %> command
 */
PUBLIC int websJstWrite(int jid, Webs *wp, int argc, char **argv)
{
    int     i;

    assert(websValid(wp));

    logmsg(2,"current=[%d]",websGetPage(wp));
    logmsg(2,"mid=%d",websGetQdata(wp,"mid") );
    logmsg(2,"page=%d",websGetQdata(wp,"page") );

    for (i = 0; i < argc; ) {
        assert(argv);
         if (websWriteBlock(wp, argv[i], strlen(argv[i])) < 0) {
             logmsg(2,"argv[%d]=%s",i,argv[i]);
             return -1;
         }
        if (++i < argc) {
            if (websWriteBlock(wp, " ", 1) < 0) {
                return -1;
            }
        }
    }
    return 0;
}

/*
    Javascript write command. This implemements <% get_title(1,1,1); %> command
 */
PUBLIC int websJstGetTitle(int jid, Webs *wp, int argc, char **argv)
{
   char strpage[TITLE_MAX];
   int pg=0,id=0,op=0;
   char sTitle[TITLE_MAX]="\0";
   
   assert(websValid(wp));
   
   id=websGetQdata(wp,"mid");
    pg=websGetPage(wp);
   if(argc ) {
      assert(argv);
       op=_atoi(argv[0]);
   }
   if(pg==0){
       sqlGetTable("tTxt", sqlGetText(sqlGetList(id,op)),"txt_title",sTitle,sizeof(sTitle));
       snprintf(strpage,sizeof(strpage),"%s",sTitle);
    } else{
        if(op==op){
            sqlGetTable("tTxt", sqlGetText(id),"txt_title",sTitle,sizeof(sTitle));
            snprintf(strpage,sizeof(strpage),"%s",sTitle);
        }
    }

    if (websWriteBlock(wp, strpage, strlen(strpage)) < 0) {
        return -1;
    }
    return 0;
}

/*
    Javascript write command. This implemements <% get_text(1,1,1); %> command
 */
PUBLIC int websJstGetSummary(int jid, Webs *wp, int argc, char **argv)
{
    int pg=0,id=0,op=0;
    int txt_id=0;

    char * pErrMsg = 0;
    int ret = 0;
    char *zSQL;
    int nrow = 0;
    int ncolumn = 0;
    char **chAllResult; //Array for Result
    const char * sSelect_lst = "SELECT txt_summary FROM tTxt where id=%d;";

    assert(websValid(wp));
    
    id=websGetQdata(wp,"mid");
    pg=websGetPage(wp);
    if(argc ) {
       assert(argv);
        op=_atoi(argv[0]);
    }
    if(pg==0){
        txt_id=sqlGetText(sqlGetList(id,op));
     } else if ((pg==1)||(pg==3)){
         if(op==0){
            txt_id=sqlGetText(id);
         }else{
             txt_id=sqlGetText(id);
         }
     }
     
    zSQL = sqlite3_mprintf(sSelect_lst,txt_id);
    printf( "%s\n",zSQL);
    ret = sqlite3_get_table( sqldb, zSQL, &chAllResult , &nrow , &ncolumn , &pErrMsg );
    sqlite3_free(zSQL);
    if(ret != SQLITE_OK){
      printf("SELECT txt_content FROM tTxt where id=%d error: %s\n", txt_id,pErrMsg);
      sqlite3_free(pErrMsg);
      return -1;
    } else {
      if(nrow==1){
           websWriteBlock(wp, chAllResult[1], strlen(chAllResult[1])) ;
      } else {
          printf( "No txt_content from tTxt[%d] !\n",txt_id);
      }
    }
    sqlite3_free_table(chAllResult);

     return 0;
}

/*
    Javascript write command. This implemements <% get_text(1,1,1); %> command
 */
PUBLIC int websJstGetContent(int jid, Webs *wp, int argc, char **argv)
{
    int id=0;
    int txt_id=0;

    char * pErrMsg = 0;
    int ret = 0;
    char *zSQL;
    int nrow = 0;
    int ncolumn = 0;
    char **chAllResult; //Array for Result
    const char * sSelect_lst = "SELECT txt_content FROM tTxt where id=%d;";

    assert(websValid(wp));

    id=websGetQdata(wp,"mid");
    txt_id=sqlGetText(id);
    //printf( "id[%d]pg[%d]op[%d]txtid[%d]\n",id,pg,op,txt_id);

    zSQL = sqlite3_mprintf(sSelect_lst,txt_id);
    printf( "%s\n",zSQL);
    ret = sqlite3_get_table( sqldb, zSQL, &chAllResult , &nrow , &ncolumn , &pErrMsg );
    sqlite3_free(zSQL);
    if(ret != SQLITE_OK){
      printf("SELECT txt_content FROM tTxt where id=%d error: %s\n", txt_id,pErrMsg);
      sqlite3_free(pErrMsg);
      return -1;
    } else {
      if(nrow==1){
           websWriteBlock(wp, chAllResult[1], strlen(chAllResult[1])) ;
      } else {
          printf( "No txt_content from tTxt[%d] !\n",txt_id);
      }
    }
    sqlite3_free_table(chAllResult);

     return 0;
}

/*
    Javascript write command. This implemements <% get_auth(1,1,1); %> command
 */
PUBLIC int websJstGetAuth(int jid, Webs *wp, int argc, char **argv)
{
    char strpage[24];
    int pg=0,id=0,op=0;
    char sAuth[20]="\0";
    
    assert(websValid(wp));
    
    id=websGetQdata(wp,"mid");
    pg=websGetPage(wp);
    if(argc ) {
       assert(argv);
        op=_atoi(argv[0]);
    }
    if(pg==0){
        sqlGetTable("tPic", sqlGetPicture(sqlGetList(id,op)),"author",sAuth,sizeof(sAuth));
        snprintf(strpage,sizeof(strpage),"%s",sAuth);
     } else if ((pg==1)||(pg==3)){
         if(op==0){
             sqlGetTable("tTxt", sqlGetText(id),"author",sAuth,sizeof(sAuth));
             snprintf(strpage,sizeof(strpage),"%s",sAuth);
         }else {
             sqlGetTable("tTxt", sqlGetText(id),"author",sAuth,sizeof(sAuth));
             snprintf(strpage,sizeof(strpage),"%s",sAuth);
         }
     }
    
     if (websWriteBlock(wp, strpage, strlen(strpage)) < 0) {
         return -1;
     }
     return 0;
}

/*
    Javascript write command. This implemements <% get_time(1,1,1); %> command
 */
PUBLIC int websJstGetTime(int jid, Webs *wp, int argc, char **argv)
{
    char strpage[24];
    int pg=0,id=0,op=0;
    char sTime[20]="\0";
    
    assert(websValid(wp));
    
    id=websGetQdata(wp,"mid");
    pg=websGetPage(wp);
    if(argc ) {
       assert(argv);
        op=_atoi(argv[0]);
    }

    if(pg==0){
        sqlGetTable("tLst", sqlGetList(id,op),"lst_time",sTime,sizeof(sTime));
        snprintf(strpage,sizeof(strpage),"%s",sTime);
     } else if ((pg==1)||(pg==3)){
        if(op==0){
            sqlGetTable("tTxt",sqlGetText(id),"txt_time",sTime,sizeof(sTime));
            snprintf(strpage,sizeof(strpage),"%s",sTime);
        }else {
            sqlGetTable("tTxt",sqlGetText(id),"txt_time",sTime,sizeof(sTime));
            snprintf(strpage,sizeof(strpage),"%s",sTime);
        }
     }
    
     if (websWriteBlock(wp, strpage, strlen(strpage)) < 0) {
         return -1;
     }
     return 0;
}

/*
    Javascript write command. This implemements <% get_pic(1,1,1); %> command
 */
PUBLIC int websJstGetPicture(int jid, Webs *wp, int argc, char **argv)
{
    char strpage[32];
    int pg=0,id=0,op=0;
    int pic;
    
    assert(websValid(wp));
    
    id=websGetQdata(wp,"mid");
    pg=websGetPage(wp);
    if(argc ) {
       assert(argv);
        op=_atoi(argv[0]);
    }
    if(pg==0){
         pic=sqlGetPicture(sqlGetList(id,op));
        snprintf(strpage,sizeof(strpage),"./img_files/%04d.jpg",pic);
     } else if ((pg==1)||(pg==3)){
         pic=sqlGetPicture(id);
        snprintf(strpage,sizeof(strpage),"./img_files/0%04d.jpg",pic);
     }
     //printf("pic=%s\n",strpage);
    
     if (websWriteBlock(wp, strpage, strlen(strpage)) < 0) {
         return -1;
     }
     return 0;
}

/*
    Javascript write command. This implemements <% get_page(1,1,1); %> command
 */
PUBLIC int websJstGetPage(int jid, Webs *wp, int argc, char **argv)
{
    char strpage[16];
    int id=0,op=0,pg=0;
    
    assert(websValid(wp));
    
    id=websGetQdata(wp,"mid");
    if(argc ) {
       assert(argv);
        op=_atoi(argv[0]);
    }
    
    pg=websGetPage(wp);
    if(pg==0){
        snprintf(strpage,sizeof(strpage),"%04d",sqlGetList(id,op));
    } else {
        snprintf(strpage,sizeof(strpage),"%04d",id);
    }
   //printf("page=%s\n",strpage);
    
     if (websWriteBlock(wp, strpage, strlen(strpage)) < 0) {
         return -1;
     }
     return 0;
}

/*
    Javascript write command. This implemements <% get_ch(1,1,1); %> command
 */
PUBLIC int websJstGetChannel(int jid, Webs *wp, int argc, char **argv)
{
    int pg=0,id=0,ch=0;
   
   assert(websValid(wp));

    id=websGetQdata(wp,"mid");

#if 0
   if(argc ) {
      assert(argv);
       op=_atoi(argv[0]);
       //logmsg(2,"argv=%s",argv[0]);
   }
   
   pg=websGetPage(wp);
   if(pg){
        ch=sqlGetChannel(id);
    } else {
        ch=id;
    }
   
  //logmsg(2,"pg[%d]id[%d]op[%d]ch[%d]",pg,id,op,ch);

   if(op==ch){
       websWrite(wp, "%s","on");
    } else {
       websWrite(wp, "%s","off");
    }
#else
    pg=websGetPage(wp);
    if(pg){
         ch=sqlGetChannel(id);
     } else {
         ch=id;
     }
     websWrite(wp, "%d",ch);
#endif
    return 0;
}

PUBLIC int websJstGetShow(int jid, Webs *wp, int argc, char **argv)
{
     int pg=0,id=0,op=0;
     char sAuth[20]="\0";
    char *username;
 
    assert(websValid(wp));

    id=websGetQdata(wp,"mid");
    pg=websGetPage(wp);
    if(argc ) {
       assert(argv);
        op=_atoi(argv[0]);
    }
    if(pg==0){
        sqlGetTable("tPic", sqlGetPicture(sqlGetList(id,op)),"author",sAuth,sizeof(sAuth));
     } else if ((pg==1)||(pg==3)){
         if(op==0){
             sqlGetTable("tTxt", sqlGetText(id),"author",sAuth,sizeof(sAuth));
         }else {
             sqlGetTable("tTxt", sqlGetText(id),"author",sAuth,sizeof(sAuth));
         }
     }

    username=websGetSessionVar(wp, WEBS_SESSION_USERNAME, "");
    
    if(scaselessmatch(username,sAuth))
    {
        logmsg(2,"Authenticate[%s]sAuth[%s]",username,sAuth);
    } else {
        websWrite(wp, "%s","hidden");
        logmsg(2,"Not Authenticate[%s]sAuth[%s]",username,sAuth);
    }

     return 0;
}

PUBLIC int websJstGetLogin(int jid, Webs *wp, int argc, char **argv)
{
     char *username;

    assert(websValid(wp));

    username=websGetSessionVar(wp, WEBS_SESSION_USERNAME, 0);
    
    if(username)
    {
        websWrite(wp, "%s",username);
    }
    else
    {
        websWrite(wp, "%s","none");
    }
    
     return 0;
}

/*
    Javascript write command. This implemements <% get_ip(1,1,1); %> command
 */
PUBLIC int websJstGetContentIP(int jid, Webs *wp, int argc, char **argv)
{
    assert(websValid(wp));

    if(smatch(wp->host,"localhost"))
     {
        websWrite(wp, "%s",wp->host);
     }else{
        websWrite(wp, "%s",contentip);
     }
     //printf("ipaddr=%s\n",wp->ipaddr);
     //printf("contentip=%s\n",contentip);
    
    return 0;
}


/*
    Find s2 in s1. We skip leading white space in s1.  Return a pointer to the location in s1 after s2 ends.
 */
static char *strtokcmp(char *s1, char *s2)
{
    ssize     len;

    s1 = skipWhite(s1);
    len = strlen(s2);
    for (len = strlen(s2); len > 0 && (tolower((uchar) *s1) == tolower((uchar) *s2)); len--) {
        if (*s2 == '\0') {
            return s1;
        }
        s1++;
        s2++;
    }
    if (len == 0) {
        return s1;
    }
    return NULL;
}


static char *skipWhite(char *s) 
{
    assert(s);

    if (s == NULL) {
        return s;
    }
    while (*s && isspace((uchar) *s)) {
        s++;
    }
    return s;
}

static int strgetname(const char *s1, char *s2, int size)
{
    int i,j;
   for(i=0;;i++) {
        if(s1[i]==0) {
            s2[0]=0;
            return -1;
        }else  if(s1[i]=='/') {
            break;
        }
    }
   for(j=0;j<(size-1);j++){
        ++i;
        if(s1[i]!='.'){
            s2[j]=s1[i];
        }
        else {
            s2[j]=0;
            return _atoi( s2);
        }
    }
   s2[j]=0;
   return -1;
}

static int _atoi(const char *s)
{
    int value = 0;
    while(s && *s>='0' && *s<='9')
    {
        value *= 10;
        value += *s - '0';
        s++;
    }
    return value;
}

void substr_UTF8(char *s_input, int width, char *s_output, int buffer_len) {
                                             //
    int x1 = 11, x2 = 16, x3 = 21, x4 = 21;  //
    int charint;
    int l = 0;
    int w = 0;
    int add_width = 0;
    int add_byte = 0;
    int i=0;
    int cut=0;

    while(s_input[l]) {

        
        charint = (unsigned char)s_input[l];  //
        
        if (charint <= 127) {  //
            add_width = x1;
            add_byte = 1;
        } else if (charint >= 192 && charint <= 223) {  //
            add_width = x2;
            add_byte = 2;
        } else if (charint >= 224 && charint <= 239) {  //
            add_width = x3;
            add_byte = 3;
        } else if (charint >= 240 && charint <= 247) {  //
            add_width = x4;
            add_byte = 4;
        }

        if ((w + add_width) >= width || (l + add_byte) >= (buffer_len-4)) {
            cut = 1;
            break;
        }
        w += add_width;
        l += add_byte;
    }
    
    //printf("w=%d,l=%d\n", w, l);

    for (i = 0; i < l; i++) {
        s_output[i] = s_input[i];
    }
    if(cut){
        s_output[i++] = '.';
        s_output[i++] = '.';
        s_output[i++] = '.';
    }
    s_output[i] = 0;
}

static int websGetPage( Webs *wp)
{
    char strpage[16];
    int pg=0;
    
    assert(websValid(wp));

    pg=strgetname(wp->path,strpage,sizeof(strpage));
    if(scaselessmatch(strpage,"index")){
        pg = 0;
    } else if (scaselessmatch(strpage,"page")){
        pg = 1;
    } else if (scaselessmatch(strpage,"upload")){
        pg = 2;
    } else if (scaselessmatch(strpage,"templet_a")){
        pg = 3;
    }
        
    return pg;
}

static int websGetQdata(Webs *wp, char *option)
{
#if 0
    WebsKey         *s;
    int value=0;
    
    assert(websValid(wp));

   for (s = hashFirst(wp->vars); s; s = hashNext(wp->vars, s)) {
      //logmsg(2,"websGetQdata:%s=%s",s->name.value.string,s->content.value.string);
        if(scaselessmatch(s->name.value.string,option)) {
            //logmsg(2,"websGetQdata:%s=%s",s->name.value.string,s->content.value.string);
            value=_atoi(s->content.value.string);
            break;
        }
    }
   return value;
#else
     char *getdata;
     assert(websValid(wp));

    getdata=websGetVar(wp,option,"");
    return _atoi(getdata);
#endif
}
#if USING_SQLITE
static int sql_callback(void * data, int col_count, char ** col_values, char ** col_Name)
{
    // call back every list
    int i;
    for( i=0; i < col_count; i++){
        //printf( "%s=%s ", col_Name[i], col_values[i] == 0 ? "NULL" : col_values[i] );
    }
    //printf( "\n");
    return 0;
}
static int sqlGetList(int channel, int item)
{
  char * pErrMsg = 0;
  int ret = 0;
  int list=0;
  char *zSQL;
  int nrow = 0;
  int ncolumn = 0;
  char **chAllResult; //Array for Result
  const char * sSelect_lst = "SELECT max(id) FROM tLst where channel=%d and item=%d;";
  int c=channel,i=item;
  if  ( c==0 ||c>4 )
    c=1;

  // List table
  sqlite3_exec( sqldb, "SELECT * FROM tLst;", sql_callback, 0, &pErrMsg);

  zSQL = sqlite3_mprintf(sSelect_lst,c,i);
  ret = sqlite3_get_table( sqldb, zSQL, &chAllResult , &nrow , &ncolumn , &pErrMsg );
  sqlite3_free(zSQL);
  if(ret != SQLITE_OK){
    printf("SELECT tLst error: %s\n", pErrMsg);
    sqlite3_free(pErrMsg);
  } else {
    if(nrow==1){
        list= _atoi(chAllResult[1]);
    } else {
        printf( "No list!ch[%d]it[%d]!\n",channel,item);
    }
    #if 0
    for(int  i=0; i < nrow; i++){
        for(int  j=0; j < ncolumn; j++){
            printf( "%s=%s ", chAllResult[j], chAllResult[(i+1)*ncolumn+j] );
        }
        printf("\n");
    }
    #endif
  }
  sqlite3_free_table(chAllResult);

  return list;
}
static int sqlGetNextList(int current_lst, int flag)
{
  char * pErrMsg = 0;
  int ret = 0;
  int channel=0,item=0;
  
  int list=0;
  char *zSQL;
  int nrow = 0;
  int ncolumn = 0;
  char **chAllResult; //Array for Result
  const char * sSelect_lst = "SELECT %s(id) FROM tLst where channel=%d and item=%d and id%s%d;";
  channel= sqlGetChannel(current_lst);
  item= sqlGetItem(current_lst);

  if(channel&&item&&current_lst){
      // List table

      zSQL = sqlite3_mprintf(sSelect_lst,(flag)?"max":"min",channel,item,(flag)?"<":">",current_lst);
      logmsg(2,"%s",zSQL);
      ret = sqlite3_get_table( sqldb, zSQL, &chAllResult , &nrow , &ncolumn , &pErrMsg );
      sqlite3_free(zSQL);
      if(ret != SQLITE_OK){
        printf("SELECT tLst error: %s\n", pErrMsg);
        sqlite3_free(pErrMsg);
      } else {
        if(nrow==1){
            list= _atoi(chAllResult[1]);
        } else {
            printf( "No list!ch[%d]it[%d]!\n",channel,item);
        }
      }
      sqlite3_free_table(chAllResult);
   } 
  
   if(list==0) {
        list = current_lst;
    }

  return list;
}

static int sqlGetPicture(int list)
{
    char id[8]="\0";
    sqlGetTable("tLst",list,"pic_id",id,sizeof(id));

    return _atoi(id);
}
static int sqlGetText(int list)
{
    char id[8]="\0";
    sqlGetTable("tLst",list,"txt_id",id,sizeof(id));

    return _atoi(id);
}

static int sqlGetChannel(int list)
{
    char id[8]="\0";
    sqlGetTable("tLst",list,"channel",id,sizeof(id));

    return _atoi(id);
}

static int sqlGetItem(int list)
{
    char id[8]="\0";
    sqlGetTable("tLst",list,"item",id,sizeof(id));

    return _atoi(id);
}

static int sqlGetTable(const char *table,  int id, const char *name, char * value, int size )
{
    char * pErrMsg = 0;
    int ret = 0;
    char *zSQL;
    int nrow = 0;
    int ncolumn = 0;
    char **chAllResult; //Array for Result
    const char * sSelect_lst = "SELECT %s FROM %s where id=%d;";

    zSQL = sqlite3_mprintf(sSelect_lst,name,table,id);
    //printf( "%s\n",zSQL);
    ret = sqlite3_get_table( sqldb, zSQL, &chAllResult , &nrow , &ncolumn , &pErrMsg );
    sqlite3_free(zSQL);
    if(ret != SQLITE_OK){
        printf("SELECT %s from %s[%d] error: %s\n", name,table,id,pErrMsg);
        sqlite3_free(pErrMsg);
        return -1;
    } else {
        if(nrow==1){
            snprintf(value,size,"%s",chAllResult[1]);
        } else {
            printf( "No %s from %s[%d] !\n", name,table,id);
        }
    }
  sqlite3_free_table(chAllResult);

  return 0;
}

static int sqlGetMax(const char *table)
{
  char * pErrMsg = 0;
  int ret = 0;
  int id=1;
  char *zSQL;
  int nrow = 0;
  int ncolumn = 0;
  char **chAllResult; //Array for Result
  const char * sSelect_lst = "SELECT max(id) FROM %s ;";

  zSQL = sqlite3_mprintf(sSelect_lst,table);
  ret = sqlite3_get_table( sqldb, zSQL, &chAllResult , &nrow , &ncolumn , &pErrMsg );
  sqlite3_free(zSQL);
  if(ret != SQLITE_OK){
    printf("SELECT max(id) from %s error: %s\n",table, pErrMsg);
    sqlite3_free(pErrMsg);
  } else {
    if(nrow==1){
        id= _atoi(chAllResult[1]);
    } else {
        printf( "No id!table[%s]!\n",table);
    }
  }
  sqlite3_free_table(chAllResult);

  return id;
}

#endif

#endif /* ME_GOAHEAD_JAVASCRIPT */

#if USING_SQLITE
int CreateTable(sqlite3 * db)
{
  char * pErrMsg = 0;
  int ret = 0;
  int i,j;
  char *zSQL;
  int nrow = 0;
  int ncolumn = 0;
  char **chAllResult; //Array for Result

  const char * sCreate_lst = "create table if not exists tLst(id INTEGER PRIMARY KEY,\
 lst_time TIMESTAMP default (datetime('now', 'localtime')),\
 pic_id int, txt_id int, channel int, item int, pic_ft1 varchar(10), pic_ft2 varchar(10));";
  const char * sCreate_pic = "create table if not exists tPic(id INTEGER PRIMARY KEY,\
 pic_time TIMESTAMP default (datetime('now', 'localtime')),\
 lst_id int, author varchar(20));";
  const char * sCreate_txt = "create table if not exists tTxt(id INTEGER PRIMARY KEY,\
 txt_time TIMESTAMP default (datetime('now', 'localtime')),\
 lst_id int, author varchar(20), txt_title varchar(30), txt_summary varchar(50), txt_content TEXT);";

  //(SELECT max(pic_id) FROM tPic)+1 '640x330','800x600' '150x120','800x600'
  const char * sInsert_lst = "insert into tLst values(NULL,(SELECT datetime('now', 'localtime')),\
 '%d','%d','%d','%d','150x120','800x600');";
  const char * sInsert_pic = "insert into tPic values(NULL,(SELECT datetime('now', 'localtime')),\
 '%d', 'admin');";
  const char * sInsert_txt = "insert into tTxt values(NULL,(SELECT datetime('now', 'localtime')),\
 '%d', 'admin','%d','%d','%d');";

  const char * sUpdate_lst = "update tLst set pic_ft1='640x330' where item=1;";
  const char * sUpdate_lst1 = "update tLst set pic_ft1='640x140' where item=7;";
  const char * sUpdate_lst2 = "update tLst set pic_ft1='640x140' where item=8;";

  if(db==0)
  return -1;

  ret=sqlite3_exec( db, sCreate_lst, 0, 0, &pErrMsg );
  if( ret != SQLITE_OK ){
    printf("Create tLst error: %s\n", pErrMsg);
    sqlite3_free(pErrMsg);
    return -1;
  }
  ret=sqlite3_exec( db, sCreate_pic, 0, 0, &pErrMsg );
  if( ret != SQLITE_OK ){
    printf("Create tPic  error: %s\n", pErrMsg);
    sqlite3_free(pErrMsg);
    return -1;
  }
  ret=sqlite3_exec( db, sCreate_txt, 0, 0, &pErrMsg );
  if( ret != SQLITE_OK ){
    printf("Create tTxt  error: %s\n", pErrMsg);
    sqlite3_free(pErrMsg);
    return -1;
  }
  printf("Create Table.\n");

  zSQL = sqlite3_mprintf("select * from %s","tLst");
  ret = sqlite3_get_table( db, zSQL, &chAllResult , &nrow , &ncolumn , &pErrMsg );
  if(ret != SQLITE_OK){
    printf("Insert tLst error: %s\n", pErrMsg);
    sqlite3_free(pErrMsg);
    return -1;
  } else {
    sqlite3_free(zSQL);
    if(nrow<MAX_CH*MAX_ITEM){ //rc== SQLITE_OK\uff0cnrow=0\uff0ctable is NULL
      for(i=0;i<MAX_CH;i++){
        for(j=0;j<MAX_ITEM;j++){
          zSQL = sqlite3_mprintf(sInsert_lst,(i*MAX_ITEM+j+1),(i*MAX_ITEM+j+1),i+1,j+1);
          ret = sqlite3_exec( db, zSQL, 0, 0, &pErrMsg);
          sqlite3_free(zSQL);
          if(ret != SQLITE_OK){
            printf("Insert tLst error: %s\n", pErrMsg);
            sqlite3_free(pErrMsg);
            return -1;
          }
        }
      }
      ret = sqlite3_exec( db, sUpdate_lst, 0, 0, &pErrMsg);
      if(ret != SQLITE_OK){
        printf("Update tLst error: %s\n", pErrMsg);
        sqlite3_free(pErrMsg);
        return -1;
      }
      ret = sqlite3_exec( db, sUpdate_lst1, 0, 0, &pErrMsg);
      if(ret != SQLITE_OK){
        printf("Update tLst error: %s\n", pErrMsg);
        sqlite3_free(pErrMsg);
        return -1;
      }
      ret = sqlite3_exec( db, sUpdate_lst2, 0, 0, &pErrMsg);
      if(ret != SQLITE_OK){
        printf("Update tLst error: %s\n", pErrMsg);
        sqlite3_free(pErrMsg);
        return -1;
      }
    } else {
      for( i=0; i < nrow; i++){
        for( j=0; j < ncolumn; j++){
          //printf( "%s=%s ", chAllResult[j], chAllResult[(i+1)*ncolumn+j] );
        }
        //printf("\n");
      }
    }
    sqlite3_free_table(chAllResult);
  }

  zSQL = sqlite3_mprintf("select * from %s","tPic");
  ret = sqlite3_get_table( db, zSQL, &chAllResult , &nrow , &ncolumn , &pErrMsg );
  sqlite3_free(zSQL);
  if(ret != SQLITE_OK){
    printf("Insert tPic error: %s\n", pErrMsg);
    sqlite3_free(pErrMsg);
    return -1;
  } else {
    if(nrow<MAX_CH*MAX_ITEM){ //rc== SQLITE_OK\uff0cnrow=0\uff0ctable is NULL
      for(i=0;i<MAX_CH*MAX_ITEM;i++){
        zSQL = sqlite3_mprintf(sInsert_pic,i+1);
        ret = sqlite3_exec( db, zSQL, 0, 0, &pErrMsg);
        sqlite3_free(zSQL);
        if(ret != SQLITE_OK){
          printf("Insert tPic error: %s\n", pErrMsg);
          sqlite3_free(pErrMsg);
          return -1;
        }
      }
    } else {
      for( i=0; i < nrow; i++){
        for( j=0; j < ncolumn; j++){
          //printf( "%s=%s ", chAllResult[j], chAllResult[(i+1)*ncolumn+j] );
        }
        //printf("\n");
      }
    }
    sqlite3_free_table(chAllResult);
  }

  #if 0
  ret = sqlite3_exec( db, "alter table tTxt add txt_page TEXT", 0, 0, &pErrMsg);
  if(ret != SQLITE_OK){
    printf("alter table tTxt error: %s\n", pErrMsg);
    sqlite3_free(pErrMsg);
    return -1;
  }
  #endif

  zSQL = sqlite3_mprintf("select * from %s","tTxt");
  ret = sqlite3_get_table( db, zSQL, &chAllResult , &nrow , &ncolumn , &pErrMsg );
  sqlite3_free(zSQL);
  if(ret != SQLITE_OK){
    printf("Insert tTxtc error: %s\n", pErrMsg);
    sqlite3_free(pErrMsg);
    return -1;
  } else {
    if(nrow<MAX_CH*MAX_ITEM){ //rc== SQLITE_OK\uff0cnrow=0\uff0ctable is NULL
      for(i=0;i<MAX_CH*MAX_ITEM;i++){
        zSQL = sqlite3_mprintf(sInsert_txt,i+1,i+1,i+1,i+1);
        ret = sqlite3_exec( db, zSQL, 0, 0, &pErrMsg);
        sqlite3_free(zSQL);
        if(ret != SQLITE_OK){
          printf("Insert tTxt error: %s\n", pErrMsg);
          sqlite3_free(pErrMsg);
          return -1;
        }
      }
    } else {
      for( i=0; i < nrow; i++){
        for( j=0; j < ncolumn; j++){
         //printf( "%s=%s ", chAllResult[j], chAllResult[(i+1)*ncolumn+j] );
        }
        //printf("\n");
      }
    }
    sqlite3_free_table(chAllResult);
  }
  printf("Insert default Table.\n");
  return 0;
}

#endif

#if ME_GOAHEAD_UPLOAD
/*
    Dump the file upload details. Don't actually do anything with the uploaded file.
 */
void ExeUploadFiles(Webs *wp)
{
#if 1
    WebsKey         *s;
    WebsUpload  *up;
    char            *upfile;
    char            *upfile_s;
    char uri[256];
    int currentpage=0,nextpage=0,nextpic=0,nexttxt=0;
    char  channel=0,item=0;
    char pic_ft1[10]="150x120";
    char pic_ft2[10]="800x600";
    char *mytitle;
    char *mytext;

    char * pErrMsg = 0;
    int ret = 0;
    char *zSQL;
     const char * sInsert_lst = "insert into tLst values('%d',(SELECT datetime('now', 'localtime')),\
'%d','%d','%d','%d','%s','%s');";
      const char * sInsert_pic = "insert into tPic values('%d',(SELECT datetime('now', 'localtime')),\
'%d', '%s');";
      const char * sInsert_txt = "insert into tTxt values('%d',(SELECT datetime('now', 'localtime')),\
'%d', '%s','%s','%s','%s');";

    assert(websValid(wp));
        
if (scaselessmatch(wp->method, "POST")) {
    char document[128];
    snprintf(document,sizeof(document),"%s",websGetDocuments());
    if(document[strlen(document)-1]=='/')
        document[strlen(document)-1]='\0';
    
    currentpage=websGetQdata(wp,"mid");
    logmsg(2,"currentpage=%d\n",currentpage);
    nextpage=sqlGetMax("tLst")+1;
    nextpic=sqlGetMax("tPic")+1;
    nexttxt=sqlGetMax("tTxt")+1;
    channel=sqlGetChannel(currentpage);
    logmsg(2,"channel=%d\n",channel);
    item=sqlGetItem(currentpage);
    logmsg(2,"item=%d\n",item);
    sqlGetTable("tLst",currentpage,"pic_ft1",pic_ft1,sizeof(pic_ft1));
    sqlGetTable("tLst",currentpage,"pic_ft2",pic_ft2,sizeof(pic_ft2));

    upfile = sfmt("%s/img_files/0%04d.jpg", document,nextpic);
    logmsg(2,"%s\n",upfile);
    upfile_s = sfmt("%s/img_files/%04d.jpg", document,nextpic);
    logmsg(2,"%s\n",upfile_s);
            
    mytitle=websGetVar(wp, "mytitle", "");
    mytext=websGetVar(wp, "mytext", "");
    
    for (s = hashFirst(wp->files); s; s = hashNext(wp->files, s)) {
        up = s->content.value.symbol;
        //upfile = sfmt("%sdog_files/%s", document, up->clientFilename);
        //rename(up->filename, upfile);
        //int ret=chmod(upfile,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
        //logmsg(2,"chmod %s=%d!\n",upfile,ret);
        snprintf(uri,sizeof(uri),"convert -scale %s %s %s",pic_ft2,up->filename,upfile);
        logmsg(2,"%s\n",uri);
        system(uri);
        if(scaselessmatch(pic_ft1,"640x140") ){
            int currentpic=sqlGetPicture(currentpage);
            int lastpage=sqlGetNextList(currentpage,1);
            int lastpic=sqlGetPicture(lastpage);
            logmsg(2,"last[%d]current[%d]next[%d]\n",lastpic,currentpic,nextpic);
            char *currentfile = sfmt("%s/img_files/0%04d.jpg", document,currentpic);
            char *lastfile = sfmt("%s/img_files/0%04d.jpg", document,lastpic);
            char *tempfile = sfmt("%s/img_files/temp.jpg", document);
            char *convert=sfmt("convert %s %s %s +append %s",lastfile,currentfile,upfile,tempfile);
            //logmsg(2,"%s\n",convert);
            system(convert);
            wfree(currentfile);wfree(lastfile);wfree(tempfile);wfree(convert);

            snprintf(uri,sizeof(uri),"convert -scale %s %s %s",pic_ft1,tempfile,upfile_s);
            //snprintf(uri,sizeof(uri),"convert %s -resize 640x -gravity center -extent %s %s",upfile,pic_ft1,upfile_s);
            //logmsg(2,"%s\n",uri);
            system(uri);
            
            snprintf(uri,sizeof(uri),"rm %s",tempfile);
            //logmsg(2,"%s\n",uri);
            system(uri);
        }else{
           snprintf(uri,sizeof(uri),"convert -scale %s %s %s",pic_ft1,upfile,upfile_s);
           logmsg(2,"%s\n",uri);
            system(uri);
        }
        wfree(upfile);
        wfree(upfile_s);
    }

    zSQL = sqlite3_mprintf(sInsert_lst,nextpage,nextpic,nexttxt,channel,item,pic_ft1,pic_ft2);
    ret = sqlite3_exec( sqldb, zSQL, 0, 0, &pErrMsg);
    printf("%s\n",zSQL);
    sqlite3_free(zSQL);
    if(ret != SQLITE_OK){
          printf("Insert tLst error: %s\n", pErrMsg);
          sqlite3_free(pErrMsg);
      }
    zSQL = sqlite3_mprintf(sInsert_pic,nextpic,nextpage,wp->username);
    ret = sqlite3_exec( sqldb, zSQL, 0, 0, &pErrMsg);
    printf("%s\n",zSQL);
    sqlite3_free(zSQL);
    if(ret != SQLITE_OK){
          printf("Insert tPic error: %s\n", pErrMsg);
          sqlite3_free(pErrMsg);
      }
    zSQL = sqlite3_mprintf(sInsert_txt,nexttxt,nextpage,wp->username,mytitle,mytext,mytext);
    ret = sqlite3_exec( sqldb, zSQL, 0, 0, &pErrMsg);
    printf("%s\n",zSQL);
    sqlite3_free(zSQL);
    if(ret != SQLITE_OK){
          printf("Insert tTxt error: %s\n", pErrMsg);
          sqlite3_free(pErrMsg);
      }
    }
    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteHeader(wp, "Content-Type", "text/plain");
    websWriteHeader(wp, "Access-Control-Allow-Origin", "*");
    websWriteEndHeaders(wp);
    //websWrite(wp, "\r\nOK\r\n");
    websWrite(wp, "{\"mytitle\":\"ok\",\"mytext\":\"ok\"}");
    websDone(wp);
#else
	
    WebsKey         *s;
    WebsUpload  *up;
    char            *upfile;

    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteHeader(wp, "Content-Type", "text/plain");
    websWriteEndHeaders(wp);
    if (scaselessmatch(wp->method, "POST")) {
        for (s = hashFirst(wp->files); s; s = hashNext(wp->files, s)) {
            up = s->content.value.symbol;
            websWrite(wp, "FILE: %s\r\n", s->name.value.string);
            websWrite(wp, "FILENAME=%s\r\n", up->filename);
            websWrite(wp, "CLIENT=%s\r\n", up->clientFilename);
            websWrite(wp, "TYPE=%s\r\n", up->contentType);
            websWrite(wp, "SIZE=%d\r\n", up->size);
            upfile = sfmt("%s/tmp/%s", websGetDocuments(), up->clientFilename);
            websWrite(wp, "upfile=%s\r\n", upfile);
            rename(up->filename, upfile);
            wfree(upfile);
        }
        websWrite(wp, "\r\nVARS:\r\n");
        for (s = hashFirst(wp->vars); s; s = hashNext(wp->vars, s)) {
            websWrite(wp, "%s=%s\r\n", s->name.value.string, s->content.value.string);
		if(scaselessmatch(s->name.value.string,"option"))
			websWrite(wp, "\r\nTHIS!!!\r\n");	
        }
    }
    websDone(wp);
#endif
}
#endif

/*
    Dump the file upload details. Don't actually do anything with the uploaded file.
 */
void ExeUploadPage(Webs *wp)
{
    char            *remote_file;
    //char            *upfile;
    char            *upfile_s;
    char            *uppage;
    char            *updefault;
    //char uri[256];
    char document[128];
    int nextpage=0,nextpic=0,nexttxt=0;
    char  channel=0,item=0;
    char pic_ft1[10]="150x120";
    char pic_ft2[10]="html";
    char *mytitle;
    char *mysummary;
    char *mycontent;
    char *myname;
    char *mypw;
    char title[TITLE_MAX];
    char summary[SUMMARY_MAX];
    char *command;

    char * pErrMsg = 0;
    int ret = 0;
    char *zSQL;
     const char * sInsert_lst = "insert into tLst values('%d',(SELECT datetime('now', 'localtime')),\
'%d','%d','%d','%d','%s','%s');";
      const char * sInsert_pic = "insert into tPic values('%d',(SELECT datetime('now', 'localtime')),\
'%d', '%s');";
      const char * sInsert_txt = "insert into tTxt values('%d',(SELECT datetime('now', 'localtime')),\
'%d', '%s','%s','%s','%s');";

    assert(websValid(wp));
        
if (scaselessmatch(wp->method, "POST")) {
    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteHeader(wp, "Content-Type", "text/plain");
    websWriteHeader(wp, "Access-Control-Allow-Origin", "*");
    websWriteEndHeaders(wp);
    
    myname=websGetVar(wp, "yourname", "");
    logmsg(2,"myname=%s\n",myname);
    mypw=websGetVar(wp, "password", "");
    logmsg(2,"mypw=%s\n",mypw);

    if(0==websVerifyPasswordFromPost(myname,mypw))
    {
        logmsg(2,"VerifyPasswordFromPost NG\n");
        websWrite(wp, "{\"status\":\"VerifyPassword Failure\",\"ErrorCode\":\"0\"}");
        websDone(wp);
        return;
    }

    snprintf(document,sizeof(document),"%s",websGetDocuments());
    if(document[strlen(document)-1]=='/')
        document[strlen(document)-1]='\0';
    
    nextpage=sqlGetMax("tLst")+1;
    nextpic=sqlGetMax("tPic")+1;
    nexttxt=sqlGetMax("tTxt")+1;
    channel=_atoi(websGetVar(wp, "pagetype", ""));
    logmsg(2,"channel=%d\n",channel);
    item=_atoi(websGetVar(wp, "pagesubject", ""));
    logmsg(2,"item=%d\n",item);

    //upfile = sfmt("%s/img_files/0%04d.jpg", document,nextpic);// 
    upfile_s = sfmt("%s/img_files/%04d.jpg", document,nextpic);// thumbnail
    uppage = sfmt("%s/img_files/%04d.html", document,nextpage);// 
    updefault  = sfmt("%s/img_files/%d.jpg", document,channel);// thumbnail
    logmsg(2,"upfile_s=%s\n",upfile_s);
    remote_file = websGetVar(wp, "thumbnail", "");
    websDecodeUrl(remote_file, remote_file, strlen(remote_file));
    logmsg(2,"remote_file=%s\n",remote_file);
    if(scaselessmatch(remote_file,"default picture!")){
         command=sfmt("cp -f  %s %s",updefault,upfile_s);
        logmsg(2,"%s\n",command);
        system(command);
        wfree(command);
    }else{   // Todo  image file type  and gif
        char *ext = strrchr(remote_file,'.');
        if(scaselessmatch(ext,".jpg")||scaselessmatch(ext,".bmp")||scaselessmatch(ext,".png")){
            command=sfmt("wget -O temp %s",remote_file);
            logmsg(2,"%s\n",command);
            system(command);
            wfree(command);
            command=sfmt("convert -scale %s temp %s",pic_ft1,upfile_s);
            logmsg(2,"%s\n",command);
            system(command);
            wfree(command);
            command=sfmt("rm %s","temp");
            logmsg(2,"%s\n",command);
            system(command);
            wfree(command);
        } else if(scaselessmatch(ext,".gif")){
            command=sfmt("wget -O temp.gif %s",remote_file);
            logmsg(2,"%s\n",command);
            system(command);
            wfree(command);
            command=sfmt("convert -scale %s 'temp.gif[0]' %s",pic_ft1,upfile_s);
            logmsg(2,"%s\n",command);
            system(command);
            wfree(command);
            command=sfmt("rm %s","temp.gif");
            logmsg(2,"%s\n",command);
            system(command);
            wfree(command);
        } else {
             command=sfmt("cp -f  %s %s",updefault,upfile_s);
            logmsg(2,"%s\n",command);
            system(command);
            wfree(command);
        }
    }

    mytitle=websGetVar(wp, "pagetitle", "");
    substr_UTF8(mytitle,TITLE_MAX*FONT_size,title,sizeof(title));
    logmsg(2,"pagetitle=%s\n",title);
    mysummary=websGetVar(wp, "summary", "");
    websDecodeUrl(mysummary, mysummary, strlen(mysummary));
    substr_UTF8(mysummary,SUMMARY_MAX*FONT_size,summary,sizeof(summary));
    logmsg(2,"summary=%s\n",summary);
    mycontent=websGetVar(wp, "content", "");
    websDecodeUrl(mycontent, mycontent, strlen(mycontent));
    //logmsg(2,"mycontent=%s\n",mycontent);
    #if 0
    {
         int fd;
        if ((fd = open(uppage, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0600)) < 0) {
            logmsg(2,"openfile error=%s\n",fd );
        } else {
            int len=strlen(mycontent);
            int wlen=0;int i;
            for(i=0;i<100;i++)
            {
                wlen=write(fd, mycontent,len) ;
                len-=wlen;
                if(len==0)
                    break;
            }
            close(fd);
        }
    }
    #else
    uppage = uppage;
    #endif
    
    if (scaselessmatch(pic_ft2, "html")) {
        zSQL = sqlite3_mprintf(sInsert_lst,nextpage,nextpic,nexttxt,channel,item,pic_ft1,pic_ft2);
        ret = sqlite3_exec( sqldb, zSQL, 0, 0, &pErrMsg);
        printf("%s\n",zSQL);
        sqlite3_free(zSQL);
        if(ret != SQLITE_OK){
              printf("Insert tLst error: %s\n", pErrMsg);
              sqlite3_free(pErrMsg);
          }
        zSQL = sqlite3_mprintf(sInsert_pic,nextpic,nextpage,myname);
        ret = sqlite3_exec( sqldb, zSQL, 0, 0, &pErrMsg);
        printf("%s\n",zSQL);
        sqlite3_free(zSQL);
        if(ret != SQLITE_OK){
              printf("Insert tPic error: %s\n", pErrMsg);
              sqlite3_free(pErrMsg);
          }
        zSQL = sqlite3_mprintf(sInsert_txt,nexttxt,nextpage,myname,title,summary,mycontent);
        ret = sqlite3_exec( sqldb, zSQL, 0, 0, &pErrMsg);
        printf("%s\n",zSQL);
        sqlite3_free(zSQL);
        if(ret != SQLITE_OK){
              printf("Insert tTxt error: %s\n", pErrMsg);
              sqlite3_free(pErrMsg);
          }
        }
    }
    websWrite(wp, "{\"status\":\"ok\",\"ErrorCode\":\"1\"}");
    websDone(wp);
}

void ExeUploadTexts(Webs *wp)
{
    //WebsKey         *s;
    int currentpage=0;
    int currenttxt=0;
    char mytitle[TITLE_MAX];
    char mysummary[SUMMARY_MAX];
    char *mycontent;

    char * pErrMsg = 0;
    int ret = 0;
    char *zSQL;
    const char * sUpdate_txt = "update tTxt set txt_title='%s', txt_summary='%s',txt_content='%s'  where id=%d;";
    const char * sInsert_txt = "insert into tTxt (txt_time,lst_id,author,txt_title,txt_summary,txt_content) values((SELECT datetime('now', 'localtime')),'%d', '%s', 'comment', '%s', 'comment');";
    // "insert into table1 (data,time,.....) values ('xxxxx','xxxx',....)"
#if 0
    const char * sCreate_txt = "create table if not exists tTxt(id INTEGER PRIMARY KEY,\
     txt_time TIMESTAMP default (datetime('now', 'localtime')),\
     lst_id int, author varchar(20), txt_title varchar(20), txt_content TEXT);";
#endif

    assert(websValid(wp));
        
    if (scaselessmatch(wp->method, "POST")) {
        currentpage=websGetQdata(wp,"mid");
        currenttxt=sqlGetText(currentpage);
        printf("current[%d][%d]\n",currentpage,currenttxt);
              
        substr_UTF8(websGetVar(wp, "mytitle", ""),TITLE_MAX*FONT_size,mytitle,sizeof(mytitle));
        mycontent=websGetVar(wp, "mytext", "");
        substr_UTF8(mycontent,SUMMARY_MAX*FONT_size,mysummary,sizeof(mysummary));
        //printf("mycontent=%s\n",mycontent);
        printf("mysummary=%s\n",mysummary);

        if(scaselessmatch(mytitle,"comment")){
            zSQL = sqlite3_mprintf(sInsert_txt,currentpage,wp->username,mysummary);
            ret = sqlite3_exec( sqldb, zSQL, 0, 0, &pErrMsg);
            printf("%s\n",zSQL);
            sqlite3_free(zSQL);
            if(ret != SQLITE_OK){
                  printf("Update tTxt error: %s\n", pErrMsg);
                  sqlite3_free(pErrMsg);
             }
         } else {
            zSQL = sqlite3_mprintf(sUpdate_txt,mytitle,mysummary,mycontent,currenttxt);
            ret = sqlite3_exec( sqldb, zSQL, 0, 0, &pErrMsg);
            printf("%s\n",zSQL);
            sqlite3_free(zSQL);
            if(ret != SQLITE_OK){
                  printf("Update tTxt error: %s\n", pErrMsg);
                  sqlite3_free(pErrMsg);
             }
        }
    }
    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteHeader(wp, "Content-Type", "text/html");
    websWriteEndHeaders(wp);
    websWrite(wp, "{\"mytitle\":\"%s\",\"mytext\":\"%s\"}",mytitle,mysummary);
    websDone(wp);
}

void ExeDeletePage(Webs *wp)
{
    char            *redirect;
    int currentpage=0;
    int currenttxt=0;
    int currentpic=0;
    char  channel=0,item=0;
    char            *upfile;
    char            *upfile_s;

    char * pErrMsg = 0;
    int ret = 0;
    char *zSQL;
    const char * sDelete_table = "delete from %s where id=%d;";

    assert(websValid(wp));
    currentpage=websGetQdata(wp,"mid");
    if(currentpage>10){
        currentpic=sqlGetPicture(currentpage);
        currenttxt=sqlGetText(currentpage);
        channel=sqlGetChannel(currentpage);
        logmsg(2,"channel=%d",channel);
        item=sqlGetItem(currentpage);
        logmsg(2,"item=%d",item);

        char document[128];
        snprintf(document,sizeof(document),"%s",websGetDocuments());
        if(document[strlen(document)-1]=='/')
            document[strlen(document)-1]='\0';

        upfile = sfmt("%s/img_files/0%04d.jpg", document,currentpic);
        logmsg(2,"remove %s",upfile);
        upfile_s = sfmt("%s/img_files/%04d.jpg", document,currentpic);
        logmsg(2,"remove %s",upfile_s);
        remove(upfile);
        remove(upfile_s);
        wfree(upfile);
        wfree(upfile_s);
        
        zSQL = sqlite3_mprintf(sDelete_table,"tPic",currentpic);
        ret = sqlite3_exec( sqldb, zSQL, 0, 0, &pErrMsg);
        printf("%s\n",zSQL);
        sqlite3_free(zSQL);
        if(ret != SQLITE_OK){
              printf("Delete tPic[%d] error: %s\n", currentpic,pErrMsg);
              sqlite3_free(pErrMsg);
          }
        zSQL = sqlite3_mprintf(sDelete_table,"tTxt",currenttxt);
        ret = sqlite3_exec( sqldb, zSQL, 0, 0, &pErrMsg);
        printf("%s\n",zSQL);
        sqlite3_free(zSQL);
        if(ret != SQLITE_OK){
              printf("Delete tTxt[%d] error: %s\n", currenttxt,pErrMsg);
              sqlite3_free(pErrMsg);
          }
        zSQL = sqlite3_mprintf(sDelete_table,"tLst",currentpage);
        ret = sqlite3_exec( sqldb, zSQL, 0, 0, &pErrMsg);
        printf("%s\n",zSQL);
        sqlite3_free(zSQL);
        if(ret != SQLITE_OK){
              printf("Delete tLst[%d] error: %s\n", currentpage,pErrMsg);
              sqlite3_free(pErrMsg);
          }

        currentpage=sqlGetList(channel,item);

        redirect = sfmt("/page.jst?mid=%04d", currentpage);
    }else {
        redirect = sfmt("/page.jst?mid=%04d", currentpage);
    }
    
    logmsg(2,"redirect to %s",redirect);
    websRedirect(wp,redirect);
    wfree(redirect);

}

void ExeNextPage(Webs *wp)
{
#if 1
int currentpage=0;
int next_last=0;
int pic_id=0;
int txt_id=0;

char * pErrMsg = 0;
int ret = 0;
char *zSQL;
int nrow = 0;
int ncolumn = 0;
char **chAllResult; //Array for Result
char pagemode[20];
 
const char * sSelect_txt = "SELECT *  FROM tTxt where id=%d;";
char * sOutput =  "{\"id\":\"%d\",\"txt_time\":\"%s\",\"author\":\"%s\",\"txt_title\":\"%s\",\"txt_content\":\"%s\",\"picture\":\"./img_files/0%04d.jpg\",\"pagemode\":\"%s\"}";

assert(websValid(wp));
currentpage=websGetQdata(wp,"mid");
next_last=websGetQdata(wp,"next_last");
currentpage=sqlGetNextList(currentpage,next_last);
pic_id=sqlGetPicture(currentpage);
txt_id=sqlGetText(currentpage);
sqlGetTable("tLst",currentpage,"pic_ft2",pagemode,sizeof(pagemode));

if (scaselessmatch(wp->method, "POST")) {
    printf("nextPage:POST\n");
    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteHeader(wp, "Content-Type", "text/html");
    websWriteEndHeaders(wp);

    zSQL = sqlite3_mprintf(sSelect_txt,txt_id);
    logmsg(2,"%s",zSQL);
    ret = sqlite3_get_table( sqldb, zSQL, &chAllResult , &nrow , &ncolumn , &pErrMsg );
    sqlite3_free(zSQL);
    if(ret != SQLITE_OK){
      printf("SELECT tTxt error: %s\n", pErrMsg);
      sqlite3_free(pErrMsg);
    } else {
      if(nrow==1){
        if(scaselessmatch(pagemode,"html")){
            printf( "Page[%d][%s][%s][%s][%s][%d][%s]\n",txt_id,chAllResult[ncolumn+1],chAllResult[ncolumn+3],chAllResult[ncolumn+4],"content",0,pagemode);
            //sqlGetTable("tTxt", sqlGetText(currentpage),"author",sAuth,sizeof(sAuth));
            websWrite(wp,sOutput,currentpage,chAllResult[ncolumn+1],chAllResult[ncolumn+3],chAllResult[ncolumn+4],"content",0,pagemode);
        }else{
            printf( "Page[%d][%s][%s][%s][%s][%d][%s]\n",txt_id,chAllResult[ncolumn+1],chAllResult[ncolumn+3],chAllResult[ncolumn+4],chAllResult[ncolumn+6],pic_id,pagemode);
            //sqlGetTable("tTxt", sqlGetText(currentpage),"author",sAuth,sizeof(sAuth));
            websWrite(wp,sOutput,currentpage,chAllResult[ncolumn+1],chAllResult[ncolumn+3],chAllResult[ncolumn+4],chAllResult[ncolumn+6],pic_id,pagemode);
        }
      }
    }
    sqlite3_free_table(chAllResult);

    websDone(wp);
}else{
    char            *redirect;
    printf("nextPage:GET\n");
    redirect = sfmt("/page.jst?mid=%04d", currentpage);
    logmsg(2,"redirect to %s",redirect);
    websRedirect(wp,redirect);
    wfree(redirect);
}
#else
    char            *redirect;
    int currentpage=0;
    int flag=0;

    assert(websValid(wp));
    currentpage=websGetQdata(wp,"mid");
    flag=websGetQdata(wp,"next_last");
    currentpage=sqlGetNextList(currentpage,flag);
    redirect = sfmt("/page.jst?mid=%04d", currentpage);
    logmsg(2,"redirect to %s",redirect);
    websRedirect(wp,redirect);
    wfree(redirect);
#endif
}

void ExeReturnHome(Webs *wp)
{
    char            *redirect;
    int currentpage=0;
    int channel =1;

    assert(websValid(wp));
    currentpage=websGetQdata(wp,"mid");
    channel=sqlGetChannel(currentpage);
    redirect = sfmt("/index.jst?mid=%01d", channel);
    logmsg(2,"redirect to %s",redirect);
    websRedirect(wp,redirect);
    wfree(redirect);
}

#if 0
bool isValidIP(char *ip){
    if(ip==NULL)
        return false;
    char temp[4];
    int count=0;
    while(true){
        int index=0;
        while(*ip!='\0' && *ip!='.' && count<4){
            temp[index++]=*ip;
            ip++;
        }
        if(index==4)
            return false;
        temp[index]='\0';
        int num=atoi(temp);
        if(!(num>=0 && num<=255))
            return false;
        count++;
        if(*ip=='\0'){
            if(count==4)
                return true;
            else
                return false;
        }else
            ip++;
    }
}
#endif

void ExeUploaderIP(Webs *wp)
{
    //memset(&adr_inet, 0, sizeof(adr_inet));
    //adr_inet.sin_family = AF_INET;
    //adr_inet.sin_port = htons(5000); 

    assert(websValid(wp));
    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteHeader(wp, "Content-Type", "text/html");
    websWriteEndHeaders(wp);
    if( inet_aton(wp->input.servp, &adr_inet.sin_addr)){
        snprintf(contentip,sizeof(contentip),"%s",wp->input.servp);
        logmsg(2, "ipaddr IP[%s]\n",wp->ipaddr);
        logmsg(2, "Content IP[%s]\n",contentip);
        websWrite(wp,"Content IP[%s]\n",contentip);
    }  else {
        logmsg(2, "Content IP error\n");
        websWrite(wp,"Content IP error\n");
    }
    websDone(wp);
}

void ExeGetComment(Webs *wp)
{
    int currentpage=0;
    int top=0;
    int buttom=0;
    char * pErrMsg = 0;
    int ret = 0;
    char *zSQL;
    int nrow = 0;
    int ncolumn = 0;
    char **chAllResult; //Array for Result
    //char sAuth[20]="\0";
    
    const char * sSelect_txt = "SELECT *  FROM tTxt where lst_id=%d and txt_content='comment' and id%s%d ORDER BY id DESC;";
    const char * sOutputFirst =  "{\"comments\": [";
    char * sOutput =  "{\"id\":\"%s\",\"txt_time\":\"%s\",\"author\":\"%s\",\"txt_text\":\"%s\"}";
    const char * sOutputMiddle =  ",";
    const char * sOutputLast =  "]}";

    assert(websValid(wp));
    currentpage=websGetQdata(wp,"mid");
    top=websGetQdata(wp,"top");
    buttom=websGetQdata(wp,"buttom");

    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteHeader(wp, "Content-Type", "text/html");
    websWriteEndHeaders(wp);
    
    zSQL = sqlite3_mprintf(sSelect_txt,currentpage,(top)?">":"<",(top)?top:buttom);
    logmsg(2,"%s",zSQL);
    ret = sqlite3_get_table( sqldb, zSQL, &chAllResult , &nrow , &ncolumn , &pErrMsg );
    sqlite3_free(zSQL);
    if(ret != SQLITE_OK){
      printf("SELECT tTxt error: %s\n", pErrMsg);
      sqlite3_free(pErrMsg);
    } else {
      if(nrow>0){ int i;
        websWrite(wp,"%s",sOutputFirst);
        for(i=1;;i++)
        {
            printf( "Comment[%s][%s][%s][%s]\n",chAllResult[i*ncolumn],chAllResult[i*ncolumn+1],chAllResult[i*ncolumn+3],chAllResult[i*ncolumn+5]);
            //sqlGetTable("tTxt", sqlGetText(currentpage),"author",sAuth,sizeof(sAuth));
            websWrite(wp,sOutput,chAllResult[i*ncolumn],chAllResult[i*ncolumn+1],chAllResult[i*ncolumn+3],chAllResult[i*ncolumn+5]);
            if((i==nrow)||(i==5)) break;
            websWrite(wp,"%s",sOutputMiddle);
        }
        websWrite(wp,"%s",sOutputLast);
      } else {
          printf( "No txt!@page[%d]!\n",currentpage);
          websWrite(wp,"%s",sOutputFirst);
          websWrite(wp,"%s","{,}");
          websWrite(wp,"%s",sOutputLast);
      }
    }
    sqlite3_free_table(chAllResult);
    
    websDone(wp);

}

void ExeDeleteComment(Webs *wp)
{
    int txt_id=0;

    char * pErrMsg = 0;
    int ret = 0;
    char *zSQL;
    const char * sDelete_table = "delete from tTxt where id=%d;";
    char * sOutput =  "{\"commentid\":\"%d\"}";

    assert(websValid(wp));
    txt_id=websGetQdata(wp,"mid");
    if(txt_id>10){
        
        zSQL = sqlite3_mprintf(sDelete_table,txt_id);
        ret = sqlite3_exec( sqldb, zSQL, 0, 0, &pErrMsg);
        printf("%s\n",zSQL);
        sqlite3_free(zSQL);
        if(ret != SQLITE_OK){
              printf("Delete tTxt[%d] error: %s\n", txt_id,pErrMsg);
              sqlite3_free(pErrMsg);
          }
    }else {
    }
    
    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteHeader(wp, "Content-Type", "text/html");
    websWriteEndHeaders(wp);
    websWrite(wp,sOutput,txt_id);
    websDone(wp);
}

void ExeGetList(Webs *wp)
{
    int channel_id;
    int lst_id;
    
    char * pErrMsg = 0;
    int ret = 0;
    char *zSQL;
    int nrow = 0;
    int ncolumn = 0;
    char **chAllResult; //Array for Result
    
    char sPage[20]="";
    char sTime[20]="";
    char sAuth[20]="";
    char sPic[20]="";
    char sTitle[TITLE_MAX]="";
    char sSummary[SUMMARY_MAX]="";
    char sMode[20]="";
    char sMode2[20]="";
    
    const char * sSelect_toplist = "SELECT *  FROM tLst ORDER BY id DESC limit 10 offset %d;";

    const char * sOutputFirst =  "{\"index\": [";
    char * sOutput =  "{\"page\":\"%s\",\"time\":\"%s\",\"pic\":\"%s\",\"auth\":\"%s\",\"title\":\"%s\",\"summary\":\"%s\",\"mode\":\"%s\"}";
    const char * sOutputMiddle =  ",";
    const char * sOutputLast =  "]}";
    
    assert(websValid(wp));
    channel_id=_atoi(websGetVar(wp, "channel_id", ""));
    lst_id=_atoi(websGetVar(wp, "lst_id", ""));
    //printf("channel_id[%d]lst_id[%d]\n",channel_id,lst_id);
    
    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteHeader(wp, "Content-Type", "text/html");
    websWriteEndHeaders(wp);

    if(channel_id==0){
        zSQL = sqlite3_mprintf(sSelect_toplist,lst_id);
        //logmsg(2,"%s",zSQL);
        ret = sqlite3_get_table( sqldb, zSQL, &chAllResult , &nrow , &ncolumn , &pErrMsg );
        sqlite3_free(zSQL);
        if(ret != SQLITE_OK){
          printf("SELECT tTxt error: %s\n", pErrMsg);
          sqlite3_free(pErrMsg);
        } else {
          if(nrow>0){ int i;int lst,pic,txt;
            websWrite(wp,"%s",sOutputFirst);

            if(lst_id ==0){
                lst=sqlGetList(channel_id,1);
                pic=sqlGetPicture(lst);
                txt=sqlGetText(lst);

                sqlGetTable("tLst", lst,"lst_time",sTime,sizeof(sTime));
                sqlGetTable("tLst", lst,"pic_ft1",sMode,sizeof(sMode));
                sqlGetTable("tLst", lst,"pic_ft2",sMode2,sizeof(sMode2));
                sqlGetTable("tPic", pic,"author",sAuth,sizeof(sAuth));
                sqlGetTable("tTxt", txt,"txt_title",sTitle,sizeof(sTitle));
                sqlGetTable("tTxt", txt,"txt_summary",sSummary,sizeof(sSummary));
                snprintf(sPage,sizeof(sPage),"%04d",lst);
                snprintf(sPic,sizeof(sPic),"%04d",pic);
                //printf( "Index2[%s][%s][%s][%s][%s][%s][%s]\n",sPage,sTime,sPic,sAuth,sTitle,sSummary,sMode);
                websWrite(wp,sOutput,sPage,sTime,sPic,sAuth,sTitle,sSummary,sMode);
                websWrite(wp,"%s",sOutputMiddle);
            }

            for(i=1;;i++)
            {
                sqlGetTable("tPic", _atoi(chAllResult[i*ncolumn+2]),"author",sAuth,sizeof(sAuth));
                sqlGetTable("tTxt", _atoi(chAllResult[i*ncolumn+3]),"txt_title",sTitle,sizeof(sTitle));
                sqlGetTable("tTxt", _atoi(chAllResult[i*ncolumn+3]),"txt_summary",sSummary,sizeof(sSummary));
                if(smatch(chAllResult[i*ncolumn+7],"html"))
                {
                    snprintf(sMode,sizeof(sMode),"%s",chAllResult[i*ncolumn+7]);
                    //printf("sMode2: %s\n", sMode);
                } else {
                    snprintf(sMode,sizeof(sMode),"%s",chAllResult[i*ncolumn+6]);
                    //printf("sMode2: %s\n", sMode);
                }
                snprintf(sPic,sizeof(sPic),"%04d",_atoi(chAllResult[i*ncolumn+2]));
                snprintf(sPage,sizeof(sPage),"%04d",_atoi(chAllResult[i*ncolumn]));
                //printf( "Index1[%s][%s][%s][%s][%s][%s][%s]\n",chAllResult[i*ncolumn],chAllResult[i*ncolumn+1],sPic,sAuth,sTitle,sSummary,chAllResult[i*ncolumn+7]);
                websWrite(wp,sOutput,sPage,chAllResult[i*ncolumn+1],sPic,sAuth,sTitle,sSummary,sMode);
                if((i==nrow)||(i==10)) break;
                websWrite(wp,"%s",sOutputMiddle);
            }
            websWrite(wp,"%s",sOutputLast);
          } else {
              printf( "No txt!@page[%d}!\n",lst_id);
              websWrite(wp,"%s",sOutputFirst);
              websWrite(wp,"%s","{,}");
              websWrite(wp,"%s",sOutputLast);
          }
        }
        sqlite3_free_table(chAllResult);
    }else{
        int i;int lst,pic,txt;
        websWrite(wp,"%s",sOutputFirst);
        for(i=1;i<=10;i++)
        {
            lst=sqlGetList(channel_id,i);
            pic=sqlGetPicture(lst);
            txt=sqlGetText(lst);
                
            sqlGetTable("tLst", lst,"lst_time",sTime,sizeof(sTime));
            sqlGetTable("tLst", lst,"pic_ft1",sMode,sizeof(sMode));
            sqlGetTable("tLst", lst,"pic_ft2",sMode2,sizeof(sMode2));
            sqlGetTable("tPic", pic,"author",sAuth,sizeof(sAuth));
            sqlGetTable("tTxt", txt,"txt_title",sTitle,sizeof(sTitle));
            sqlGetTable("tTxt", txt,"txt_summary",sSummary,sizeof(sSummary));
            if(smatch(sMode2,"html"))
            {
                snprintf(sMode,sizeof(sMode),"%s",sMode2);
                //printf("sMode2: %s\n", sMode);
            }
            //printf("sMode: %s\n", sMode);
            snprintf(sPage,sizeof(sPage),"%04d",lst);
            snprintf(sPic,sizeof(sPic),"%04d",pic);
            //printf( "Index2[%s][%s][%s][%s][%s][%s][%s]\n",sPage,sTime,sPic,sAuth,sTitle,sSummary,sMode);
            websWrite(wp,sOutput,sPage,sTime,sPic,sAuth,sTitle,sSummary,sMode);
            if(i==10) break;
            websWrite(wp,"%s",sOutputMiddle);
        }
        websWrite(wp,"%s",sOutputLast);
    }
    
    websDone(wp);
}

/*
    @copy   default

    Copyright (c) Embedthis Software. All Rights Reserved.

    This software is distributed under commercial and open source licenses.
    You may use the Embedthis GoAhead open source license or you may acquire 
    a commercial license from Embedthis Software. You agree to be fully bound
    by the terms of either license. Consult the LICENSE.md distributed with
    this software for full details and other copyrights.

    Local variables:
    tab-width: 4
    c-basic-offset: 4
    End:
    vim: sw=4 ts=4 expandtab

    @end
 */
