/*
    file.c -- File handler
  
    This module serves static file documents
 */

/********************************* Includes ***********************************/

#include    "goahead.h"

/*********************************** Locals ***********************************/

static char   *websIndex;                   /* Default page name */
static char   *websDocuments;               /* Default Web page directory */

/**************************** Forward Declarations ****************************/

static void fileWriteEvent(Webs *wp);

/*********************************** Code *************************************/
/*
    Serve static files
 */
static bool fileHandler(Webs *wp)
{
    WebsFileInfo    info;
    char            *tmp, *date;
    ssize           nchars;
    int             code;
    extern  char contentip[32];

    assert(websValid(wp));
    assert(wp->method);
    assert(wp->filename && wp->filename[0]);

#if !ME_ROM
    if (smatch(wp->method, "DELETE")) {
        if (unlink(wp->filename) < 0) {
            websError(wp, HTTP_CODE_NOT_FOUND, "Cannot delete the URI");
        } else {
            /* No content */
            websResponse(wp, 204, 0);
        }
    } else if (smatch(wp->method, "PUT")) {
        /* Code is already set for us by processContent() */
        websResponse(wp, wp->code, 0);

    } else 
#endif /* !ME_ROM */
    {
        /*
            If the file is a directory, redirect using the nominated default page
         */
        if (websPageIsDirectory(wp)) {
            nchars = strlen(wp->path);
            if (wp->path[nchars - 1] == '/' || wp->path[nchars - 1] == '\\') {
                wp->path[--nchars] = '\0';
            }
            tmp = sfmt("%s/%s", wp->path, websIndex);
            websRedirect(wp, tmp);
            wfree(tmp);
            return 1;
        }
       //printf( "filename %s\n", wp->filename);
       //printf( "path %s\n", wp->path);
       if(wp->path[0]=='/' &&
        wp->path[1]=='u'  &&
        wp->path[2]=='e'  &&
        contentip[0]!='\0'  )
        {
           char            *redirect;
           if(smatch(wp->host,"localhost"))
            {
               redirect = sfmt("http://%s:5000/%s", wp->host,& wp->path[1]);
            }else{
               redirect = sfmt("http://%s:5000/%s", contentip,& wp->path[1]);
            }
           printf("redirect to %s\n",redirect);
           websRedirect(wp,redirect);
           wfree(redirect);
           return 1;
        }
       //printf("websPageOpen\n");
        if (websPageOpen(wp, O_RDONLY | O_BINARY, 0666) < 0) {
#if ME_DEBUG
            if (wp->referrer) {
                trace(1, "From %s", wp->referrer);
            }
#endif
            websError(wp, HTTP_CODE_NOT_FOUND, "Cannot open document for: %s", wp->path);
            return 1;
        }
        if (websPageStat(wp, &info) < 0) {
            websError(wp, HTTP_CODE_NOT_FOUND, "Cannot stat page for URL");
            return 1;
        }
        code = 200;
        // jamesvan
        if(wp->range_length<0){
            if(wp->range_begin >0){
                wp->range_length=info.size - wp->range_begin;
            }else{
                wp->range_length=info.size;
            }
        }
        if(wp->range_length<info.size){
            code = 206;
        }
        if (wp->since && info.mtime <= wp->since) {
            code = 304;
            info.size = 0;
        }
        //printf("begin[%d],len=[%d],all[%d]\n",wp->range_begin,wp->range_length,(int)info.size);
        websSetStatus(wp, code);
        websWriteHeaders(wp, info.size, 0);
        if(code == 206) {
            char temp[32]="bytes=0-";
            if(wp->range_begin>0)
                sprintf(temp,"%s",wp->contentRange);
            //printf("%s\n",temp);
            websWriteHeader(wp, "Content-Range", "bytes %s/%d", &temp[6],info.size);
        }
        if ((date = websGetDateString(&info)) != NULL) {
            websWriteHeader(wp, "Last-Modified", "%s", date);
            wfree(date);
        }
        websWriteEndHeaders(wp);

        /*
            All done if the browser did a HEAD request
         */
        if (smatch(wp->method, "HEAD")) {
            websDone(wp);
            return 1;
        }
        if ((info.size > 0)&&(wp->range_length>0)) {
            wp->range_totle=0;
            if(wp->range_begin>0)
                websPageSeek(wp, wp->range_begin, SEEK_SET);
            websSetBackgroundWriter(wp, fileWriteEvent);
        } else {
            websDone(wp);
        }
    }
    return 1;
}


/*
    Do output back to the browser in the background. This is a socket write handler.
    This bypasses the output buffer and writes directly to the socket.
 */
static void fileWriteEvent(Webs *wp)
{
    char    *buf;
    ssize   len, wrote;

    assert(wp);
    assert(websValid(wp));//jamesvan

    if(wp->range_totle >= wp->range_length){
        //printf("totle_end1=%d\n",(int)wp->range_totle);
        websDone(wp);
    }
        
    if ((buf = walloc(ME_GOAHEAD_LIMIT_BUFFER)) == NULL) {
        websError(wp, HTTP_CODE_INTERNAL_SERVER_ERROR, "Cannot get memory");
        return;
    }
    while ((len = websPageReadData(wp, buf, ME_GOAHEAD_LIMIT_BUFFER)) > 0) {
        if(len>(wp->range_length-wp->range_totle))
            len=wp->range_length-wp->range_totle;
        if ((wrote = websWriteSocket(wp, buf, len)) < 0) {
            /* May be an error or just socket full (EAGAIN) */
            websPageSeek(wp, -len, SEEK_CUR);
            break;
        }
        if (wrote != len) {
            websPageSeek(wp, - (len - wrote), SEEK_CUR);
            wp->range_totle+=wrote;
            break;
        }
        wp->range_totle+=wrote;
        if(wp->range_totle >= wp->range_length)
            break;
    }
    wfree(buf);
    if ((len <= 0)||(wp->range_totle >= wp->range_length)) {
        //printf("totle_end=%d\n",(int)wp->range_totle);
        websDone(wp);
    }
}


#if !ME_ROM
PUBLIC int websProcessPutData(Webs *wp)
{
    ssize   nbytes;

    assert(wp);
    assert(wp->putfd >= 0);
    assert(wp->input.buf);

    nbytes = bufLen(&wp->input);
    wp->putLen += nbytes;
    if (wp->putLen > ME_GOAHEAD_LIMIT_PUT) {
        websError(wp, HTTP_CODE_REQUEST_TOO_LARGE | WEBS_CLOSE, "Put file too large");
        return -1;
    }
    if (write(wp->putfd, wp->input.servp, (int) nbytes) != nbytes) {
        websError(wp, HTTP_CODE_INTERNAL_SERVER_ERROR | WEBS_CLOSE, "Cannot write to file");
        return -1;
    }
    websConsumeInput(wp, nbytes);
    return 0;
}
#endif


static void fileClose()
{
    wfree(websIndex);
    websIndex = NULL;
    wfree(websDocuments);
    websDocuments = NULL;
}


PUBLIC void websFileOpen()
{
    websIndex = sclone("index.jst");
    websDefineHandler("file", 0, fileHandler, fileClose, 0);
}


/*
    Get the default page for URL requests ending in "/"
 */
PUBLIC char *websGetIndex()
{
    return websIndex;
}


PUBLIC char *websGetDocuments()
{
    return websDocuments;
}


/*
    Set the default page for URL requests ending in "/"
 */
PUBLIC void websSetIndex(char *page)
{
    assert(page && *page);

    if (websIndex) {
        wfree(websIndex);
    }
    websIndex = sclone(page);
}


/*
    Set the default web directory
 */
PUBLIC void websSetDocuments(char *dir)
{
    assert(dir && *dir);
    if (websDocuments) {
        wfree(websDocuments);
    }
    websDocuments = sclone(dir);
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
