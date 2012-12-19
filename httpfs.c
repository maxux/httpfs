#include <stdio.h>
#include <stdlib.h>
#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#define _XOPEN_SOURCE
#include <time.h>

#include "httpfs.h"

static const char *httpfsurl = "https://www2.maxux.net/dir/";

files_t * xmlparse(curl_data_t *curl) {
	xmlDoc *doc = NULL;
	xmlXPathContext *ctx = NULL;
	xmlXPathObject *xpathObj = NULL;
	xmlNode *node = NULL;
	char *xmlattribute;
	unsigned int i;
	files_t *files = NULL;

	// loading xml
	doc = (xmlDoc *) htmlReadMemory(curl->data, strlen(curl->data), "/", "utf-8", HTML_PARSE_NOERROR);
	
	// creating xpath request
	ctx = xmlXPathNewContext(doc);
	xpathObj = xmlXPathEvalExpression((const xmlChar *) "//a", ctx);
	
	if(!xmlXPathNodeSetIsEmpty(xpathObj->nodesetval)) {
		files = (files_t*) malloc(sizeof(files_t));
		files->count = xpathObj->nodesetval->nodeNr;
		
		files->filename = (char**) malloc(sizeof(char**) * files->count);

		// reading each nodes (ul li)
		for(i = 0; i < files->count; i++) {
			node = xpathObj->nodesetval->nodeTab[i];
			
			xmlattribute = xmlGetProp(node, (const xmlChar *) "href");
			
			/* filename */
			files->filename[i] = (char*) malloc(sizeof(char) * strlen(xmlattribute) + 1);
			strcpy(files->filename[i], xmlattribute);
			
			xmlFree(xmlattribute);
		}

		xmlXPathFreeObject(xpathObj);
		
	} else printf("[-] XPath: No values\n");
	
	xmlXPathFreeContext(ctx);
	xmlFreeDoc(doc);
	
	return files;
}

size_t curl_header(char *ptr, size_t size, size_t nmemb, void *userdata) {
	curl_data_t *curl = (curl_data_t*) userdata;
	struct tm tm;

	if(!(size * nmemb))
		return 0;

	printf("[ ] CURL/Header: %s", ptr);

	if(!strncmp(ptr, "Content-Length: ", 16) || !strncmp(ptr, "Content-length: ", 16)) {
		curl->httplength = atoll(ptr + 16);
		printf("[+] CURL/Header: length %d bytes\n", curl->httplength);
	}
	
	if(!strncmp(ptr, "Last-Modified: ", 15)) {
		strptime(ptr + 15, "%a, %d %b %Y %T %Z", &tm);
		curl->lasttime = mktime(&tm);
	}

	/* Return required by libcurl */
	return size * nmemb;
}

size_t curl_body(char *ptr, size_t size, size_t nmemb, void *userdata) {
	curl_data_t *curl = (curl_data_t*) userdata;
	size_t prev;

	prev = curl->length;
	curl->length += (size * nmemb);

	/* Resize data */
	curl->data  = (char*) realloc(curl->data, (curl->length + 32));

	/* Appending data */
	memcpy(curl->data + prev, ptr, size * nmemb);

	/* Return required by libcurl */
	return size * nmemb;
}

int curl_download(char *url, curl_data_t *data, char body, size_t rangex, size_t rangey) {
	CURL *curl;
	char range[64];

	curl = curl_easy_init();

	data->data   = NULL;
	data->length = 0;

	printf("[+] CURL: %s\n", url);

	if(curl) {
		curl_easy_setopt(curl, CURLOPT_URL, url);

		curl_easy_setopt(curl, CURLOPT_WRITEDATA, data);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_body);

		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
		curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10);

		curl_easy_setopt(curl, CURLOPT_HEADERDATA, data);
		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header);

		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);

		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
		
		if(body) {
			curl_easy_setopt(curl, CURLOPT_HEADER, 0);
			
			/* data range */
			if(rangex != rangey) {
				snprintf(range, sizeof(range), "%u-%u", rangex, rangey);
				curl_easy_setopt(curl, CURLOPT_RANGE, range);
			}
			
		} else curl_easy_setopt(curl, CURLOPT_NOBODY, 1);

		data->curlcode = curl_easy_perform(curl);

		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &(data->httpcode));
		printf("[ ] CURL: code: %ld\n", data->httpcode);

		curl_easy_cleanup(curl);

		if(!data->data && !data->httplength) {
			fprintf(stderr, "[-] CURL: data is empty.\n");
			return 1;
		}

	} else return 1;

	return 0;
}


static int httpfs_getattr(const char *path, struct stat *stbuf) {
	curl_data_t data;
	char url[512];
	
	memset(stbuf, 0, sizeof(struct stat));
	
	if(!strcmp(path, "/")) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		return 0;
	}
	
	sprintf(url, "%s%s", httpfsurl, path);
	
	if(curl_download(url, &data, NOBODY))
		return -ENOENT;
	
	/* Setting flags */
	stbuf->st_mode  = S_IFREG | 0444;
	stbuf->st_size  = data.httplength;
	stbuf->st_atime = data.lasttime;
	stbuf->st_ctime = data.lasttime;
	stbuf->st_mtime = data.lasttime;
	
	return 0;
}

static int httpfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	(void) offset;
	(void) fi;

	curl_data_t data;
	files_t *files;
	unsigned int i;

	if(strcmp(path, "/") != 0)
		return -ENOENT;
	
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	if(curl_download(httpfsurl, &data, WITHDATA, NORANGE))
		return 0;
	
	if(!(files = xmlparse(&data)))
		return 0;
	
	for(i = 0; i < files->count; i++)
		filler(buf, files->filename[i], NULL, 0);
	
	
	/* freeing */
	free(data.data);
	
	for(i = 0; i < files->count; i++)
		free(files->filename[i]);
	
	free(files);

	return 0;
}

static int httpfs_open(const char *path, struct fuse_file_info *fi) {
	char url[512];
	curl_data_t data;
	(void) fi;

	sprintf(url, "%s%s", httpfsurl, path);
	
	if(curl_download(url, &data, NOBODY))
		return -ENOENT;
	
	if(data.httpcode != 200)
		return -ENOENT;
		
	return 0;
}

static int httpfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	curl_data_t data;
	size_t offto;
	char url[512];
	(void) fi;
	
	sprintf(url, "%s%s", httpfsurl, path);
	
	offto = (size < offset) ? size + offset - 1 : size - 1;
		
	if(curl_download(url, &data, WITHDATA, offset, offto))
		return -ENOENT;
	
	if(data.httpcode != 200 && data.httpcode != 206)
		return -ENOENT;
	
	memcpy(buf, data.data, data.length);
	free(data.data);
	
	return data.length;
}

static struct fuse_operations hello_oper = {
	.getattr  = httpfs_getattr,
	.readdir  = httpfs_readdir,
	.open     = httpfs_open,
	.read     = httpfs_read,
};

int main(int argc, char *argv[]) {
	return fuse_main(argc, argv, &hello_oper);
}
