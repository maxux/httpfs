#ifndef __HTTPFS_HEADER
	#define __HTTPFS_HEADER	
	
	typedef struct curl_data_t {
		char *data;
		size_t length;
		size_t httplength;
		long httpcode;
		CURLcode curlcode;
		time_t lasttime;
		
	} curl_data_t;
	
	typedef struct filens_t {
		size_t count;
		char **filename;

	} files_t;

	size_t curl_header(char *ptr, size_t size, size_t nmemb, void *userdata);
	size_t curl_body(char *ptr, size_t size, size_t nmemb, void *userdata);
	
	#define NOBODY     0, 0, 0
	#define WITHDATA   1
	#define NORANGE	   0, 0
#endif
