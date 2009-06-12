#define PDC_WIDE
#include <locale.h>
#include <math.h>
#include <memory.h>
#include <curl/curl.h>
#include <string>
#include <vector>
#include <map>
#include <curses.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

typedef struct {
	std::string url;
	std::string title;
	std::string content;
	std::string source;
	std::string author;
} ENTRY; 

typedef struct {
	char* data;     // response data from server
	size_t size;    // response size of data
} MEMFILE;

static MEMFILE*
memfopen() {
	MEMFILE* mf = (MEMFILE*) malloc(sizeof(MEMFILE));
	mf->data = NULL;
	mf->size = 0;
	return mf;
}

static void
memfclose(MEMFILE* mf) {
	if (mf->data) free(mf->data);
	free(mf);
}

static size_t
memfwrite(char* ptr, size_t size, size_t nmemb, void* stream) {
	MEMFILE* mf = (MEMFILE*) stream;
	int block = size * nmemb;
	if (!mf->data)
		mf->data = (char*) malloc(block);
	else
		mf->data = (char*) realloc(mf->data, mf->size + block);
	if (mf->data) {
		memcpy(mf->data + mf->size, ptr, block);
		mf->size += block;
	}
	return block;
}

static char*
memfstrdup(MEMFILE* mf) {
	char* buf = (char*)malloc(mf->size + 1);
	memcpy(buf, mf->data, mf->size);
	buf[mf->size] = 0;
	return buf;
}

static char utf8len_tab[256] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /*bogus*/
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /*bogus*/
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,6,6,1,1,
};

static int
utf_bytes2char(unsigned char* p) {
	int		len;

	if (p[0] < 0x80)	/* be quick for ASCII */
		return p[0];

	len = utf8len_tab[p[0]];
	if ((p[1] & 0xc0) == 0x80) {
		if (len == 2)
			return ((p[0] & 0x1f) << 6) + (p[1] & 0x3f);
		if ((p[2] & 0xc0) == 0x80) {
			if (len == 3)
				return ((p[0] & 0x0f) << 12) + ((p[1] & 0x3f) << 6)
					+ (p[2] & 0x3f);
			if ((p[3] & 0xc0) == 0x80) {
				if (len == 4)
					return ((p[0] & 0x07) << 18) + ((p[1] & 0x3f) << 12)
						+ ((p[2] & 0x3f) << 6) + (p[3] & 0x3f);
				if ((p[4] & 0xc0) == 0x80) {
					if (len == 5)
						return ((p[0] & 0x03) << 24) + ((p[1] & 0x3f) << 18)
							+ ((p[2] & 0x3f) << 12) + ((p[3] & 0x3f) << 6)
							+ (p[4] & 0x3f);
					if ((p[5] & 0xc0) == 0x80 && len == 6)
						return ((p[0] & 0x01) << 30) + ((p[1] & 0x3f) << 24)
							+ ((p[2] & 0x3f) << 18) + ((p[3] & 0x3f) << 12)
							+ ((p[4] & 0x3f) << 6) + (p[5] & 0x3f);
				}
			}
		}
	}
	/* Illegal value, just return the first byte */
	return p[0];
}

static int
utf_char2bytes(int c, unsigned char* buf) {
	if (c < 0x80) {			/* 7 bits */
		buf[0] = c;
		return 1;
	}
	if (c < 0x800) {		/* 11 bits */
		buf[0] = 0xc0 + ((unsigned)c >> 6);
		buf[1] = 0x80 + (c & 0x3f);
		return 2;
	}
	if (c < 0x10000) {		/* 16 bits */
		buf[0] = 0xe0 + ((unsigned)c >> 12);
		buf[1] = 0x80 + (((unsigned)c >> 6) & 0x3f);
		buf[2] = 0x80 + (c & 0x3f);
		return 3;
	}
	if (c < 0x200000) {		/* 21 bits */
		buf[0] = 0xf0 + ((unsigned)c >> 18);
		buf[1] = 0x80 + (((unsigned)c >> 12) & 0x3f);
		buf[2] = 0x80 + (((unsigned)c >> 6) & 0x3f);
		buf[3] = 0x80 + (c & 0x3f);
		return 4;
	}
	if (c < 0x4000000) {	/* 26 bits */
		buf[0] = 0xf8 + ((unsigned)c >> 24);
		buf[1] = 0x80 + (((unsigned)c >> 18) & 0x3f);
		buf[2] = 0x80 + (((unsigned)c >> 12) & 0x3f);
		buf[3] = 0x80 + (((unsigned)c >> 6) & 0x3f);
		buf[4] = 0x80 + (c & 0x3f);
		return 5;
	}

	/* 31 bits */
	buf[0] = 0xfc + ((unsigned)c >> 30);
	buf[1] = 0x80 + (((unsigned)c >> 24) & 0x3f);
	buf[2] = 0x80 + (((unsigned)c >> 18) & 0x3f);
	buf[3] = 0x80 + (((unsigned)c >> 12) & 0x3f);
	buf[4] = 0x80 + (((unsigned)c >> 6) & 0x3f);
	buf[5] = 0x80 + (c & 0x3f);
	return 6;
}

std::string string_to_utf8(std::string str) {
	char* ptr = (char*)str.c_str();
	size_t mbssize = strlen(ptr);
	size_t wcssize = mbssize;
	wchar_t* wcstr = new wchar_t[wcssize + 1];
	int n = 0, clen = 0, len = 0;
	mblen(NULL, 0);
	while(len < mbssize) {
		clen = mblen(ptr, MB_CUR_MAX);
		if (clen <= 0) {
			mblen(NULL, 0);
			clen = 1;
		}
		clen = mbtowc(wcstr+n++, ptr,  clen);
		if (clen <= 0) {
			mblen(NULL, 0);
			clen = 1;
		}
		len += clen;
		ptr += clen;
	}
	wcstr[n] = 0;
	wcssize = n;
	std::string ret;
	for(n = 0; n < wcssize; n++) {
		unsigned char bytes[MB_CUR_MAX];
		int len = utf_char2bytes(wcstr[n], bytes);
		bytes[len] = 0;
		ret += (char*)bytes;
	}
	delete[] wcstr;
	return ret;
}

std::string utf8_to_string(std::string str) {
	char* ptr = (char*)str.c_str();
	if (ptr[0] == (char)0xef && ptr[1] == (char)0xbb && ptr[2] == (char)0xbf)
		ptr += 3;
	size_t mbssize = strlen(ptr);
	size_t wcssize = mbssize;
	wchar_t* wcstr = new wchar_t[wcssize + 1];
	int n = 0, clen = 0, len = 0;
	while(len < mbssize) {
		int c = utf_bytes2char((unsigned char*)ptr);
		if (c == 0x301c) c = 0xff5e;
		if (c == 0x2016) c = 0x2225;
		if (c == 0x2212) c = 0xff0d;
		if (c == 0x00a2) c = 0xffe0;
		if (c == 0x00a3) c = 0xffe1;
		if (c == 0x00ac) c = 0xffe2;
		wcstr[n++] = c;
		clen = utf8len_tab[(unsigned char)*ptr];
		len += clen;
		ptr += clen;
	}
	wcstr[n] = 0;
	wcssize = n;
	mbssize = wcslen(wcstr)*8;
	char* mbstr = new char[mbssize + 1];
	clen = 0;
	len = 0;
	for(n = 0; n < wcssize; n++) {
		clen = wctomb(mbstr+len, wcstr[n]);
		len += clen <= 0 ? 1 : clen;
	}
	*(mbstr+len) = 0;
	delete[] wcstr;
	std::string ret = mbstr;
	delete[] mbstr;
	return ret;
}

std::wstring utf8_to_wstring(std::string str) {
	char* ptr = (char*)str.c_str();
	if (ptr[0] == (char)0xef && ptr[1] == (char)0xbb && ptr[2] == (char)0xbf)
		ptr += 3;
	size_t mbssize = strlen(ptr);
	size_t wcssize = mbssize;
	wchar_t* wcstr = new wchar_t[wcssize + 1];
	int n = 0, clen = 0, len = 0;
	while(len < mbssize) {
		int c = utf_bytes2char((unsigned char*)ptr);
		if (c == 0x301c) c = 0xff5e;
		if (c == 0x2016) c = 0x2225;
		if (c == 0x2212) c = 0xff0d;
		if (c == 0x00a2) c = 0xffe0;
		if (c == 0x00a3) c = 0xffe1;
		if (c == 0x00ac) c = 0xffe2;
		wcstr[n++] = c;
		clen = utf8len_tab[(unsigned char)*ptr];
		len += clen;
		ptr += clen;
	}
	wcstr[n] = 0;
	std::wstring ret = wcstr;
	delete[] wcstr;
	return ret;
}

static std::vector<std::string>
split_string(std::string src, std::string key) {
	std::vector<std::string> lines;
	std::string tmp = src;

	int idx = 0;
	while (idx < (int)tmp.length()) {
		int oldidx = idx;
		idx = tmp.find(key, idx);
		if(idx != std::string::npos) {
			std::string item = tmp.substr(oldidx, idx - oldidx);
			lines.push_back(item);
		} else {
			std::string item = tmp.substr(oldidx);
			lines.push_back(item);
			break;
		}
		idx += key.length();
	}
	return lines;
}
static std::string&
replace_string(std::string& str, const std::string from, const std::string dest) {
	std::string::size_type n, nb = 0;
	while((n = str.find(from, nb)) != std::string::npos) {
		str.replace(n, from.size(), dest);
		nb = n + dest.size();
	}
	return str;
}

static std::string
html_decode(const std::string& html) {
	std::string ret = html;
	replace_string(ret, "&gt;", ">");
	replace_string(ret, "&lt;", "<");
	replace_string(ret, "&nbsp;", " ");
	replace_string(ret, "&amp;", "&");
	replace_string(ret, "&quot;", "\"");
	replace_string(ret, "&raquo;", "\x22\xe2\x89\xab\x22\x20");
	replace_string(ret, "&laquo;", "\x22\xe2\x89\xaa\x22\x20");
	std::string::size_type n1, n2, nb = 0;
	while((n1 = ret.find("&#", nb)) != std::string::npos
	   && (n2 = ret.find(";", n1)) != std::string::npos) {
		std::string num = ret.substr(n1+2, n2-n1-2);
		if (!strcspn(num.c_str(), "0123456789")) {
			unsigned char bytes[MB_CUR_MAX];
			int len = utf_char2bytes(atol(num.c_str()), bytes);
			bytes[len] = 0;
			ret.replace(n1, n2-n1+1, (char*)bytes);
		}
		nb = n1 + 1;
	}
	return ret;
}

static int
wcwidth_ucs(wchar_t ucs) {
	/* sorted list of non-overlapping intervals of non-spacing characters */
	static const struct interval {
		unsigned short first;
		unsigned short last;
	} combining[] = {
		{ 0x0300, 0x034E }, { 0x0360, 0x0362 }, { 0x0483, 0x0486 },
		{ 0x0488, 0x0489 }, { 0x0591, 0x05A1 }, { 0x05A3, 0x05B9 },
		{ 0x05BB, 0x05BD }, { 0x05BF, 0x05BF }, { 0x05C1, 0x05C2 },
		{ 0x05C4, 0x05C4 }, { 0x064B, 0x0655 }, { 0x0670, 0x0670 },
		{ 0x06D6, 0x06E4 }, { 0x06E7, 0x06E8 }, { 0x06EA, 0x06ED },
		{ 0x0711, 0x0711 }, { 0x0730, 0x074A }, { 0x07A6, 0x07B0 },
		{ 0x0901, 0x0902 }, { 0x093C, 0x093C }, { 0x0941, 0x0948 },
		{ 0x094D, 0x094D }, { 0x0951, 0x0954 }, { 0x0962, 0x0963 },
		{ 0x0981, 0x0981 }, { 0x09BC, 0x09BC }, { 0x09C1, 0x09C4 },
		{ 0x09CD, 0x09CD }, { 0x09E2, 0x09E3 }, { 0x0A02, 0x0A02 },
		{ 0x0A3C, 0x0A3C }, { 0x0A41, 0x0A42 }, { 0x0A47, 0x0A48 },
		{ 0x0A4B, 0x0A4D }, { 0x0A70, 0x0A71 }, { 0x0A81, 0x0A82 },
		{ 0x0ABC, 0x0ABC }, { 0x0AC1, 0x0AC5 }, { 0x0AC7, 0x0AC8 },
		{ 0x0ACD, 0x0ACD }, { 0x0B01, 0x0B01 }, { 0x0B3C, 0x0B3C },
		{ 0x0B3F, 0x0B3F }, { 0x0B41, 0x0B43 }, { 0x0B4D, 0x0B4D },
		{ 0x0B56, 0x0B56 }, { 0x0B82, 0x0B82 }, { 0x0BC0, 0x0BC0 },
		{ 0x0BCD, 0x0BCD }, { 0x0C3E, 0x0C40 }, { 0x0C46, 0x0C48 },
		{ 0x0C4A, 0x0C4D }, { 0x0C55, 0x0C56 }, { 0x0CBF, 0x0CBF },
		{ 0x0CC6, 0x0CC6 }, { 0x0CCC, 0x0CCD }, { 0x0D41, 0x0D43 },
		{ 0x0D4D, 0x0D4D }, { 0x0DCA, 0x0DCA }, { 0x0DD2, 0x0DD4 },
		{ 0x0DD6, 0x0DD6 }, { 0x0E31, 0x0E31 }, { 0x0E34, 0x0E3A },
		{ 0x0E47, 0x0E4E }, { 0x0EB1, 0x0EB1 }, { 0x0EB4, 0x0EB9 },
		{ 0x0EBB, 0x0EBC }, { 0x0EC8, 0x0ECD }, { 0x0F18, 0x0F19 },
		{ 0x0F35, 0x0F35 }, { 0x0F37, 0x0F37 }, { 0x0F39, 0x0F39 },
		{ 0x0F71, 0x0F7E }, { 0x0F80, 0x0F84 }, { 0x0F86, 0x0F87 },
		{ 0x0F90, 0x0F97 }, { 0x0F99, 0x0FBC }, { 0x0FC6, 0x0FC6 },
		{ 0x102D, 0x1030 }, { 0x1032, 0x1032 }, { 0x1036, 0x1037 },
		{ 0x1039, 0x1039 }, { 0x1058, 0x1059 }, { 0x17B7, 0x17BD },
		{ 0x17C6, 0x17C6 }, { 0x17C9, 0x17D3 }, { 0x18A9, 0x18A9 },
		{ 0x20D0, 0x20E3 }, { 0x302A, 0x302F }, { 0x3099, 0x309A },
		{ 0xFB1E, 0xFB1E }, { 0xFE20, 0xFE23 }
	};
	int min = 0;
	int max = sizeof(combining) / sizeof(struct interval) - 1;
	int mid;

	if (ucs == 0)
		return 0;

	/* test for 8-bit control characters */
	if (ucs < 32 || (ucs >= 0x7f && ucs < 0xa0))
		return -1;

	/* first quick check for Latin-1 etc. characters */
	if (ucs < combining[0].first)
		return 1;

	/* binary search in table of non-spacing characters */
	while (max >= min) {
		mid = (min + max) / 2;
		if (combining[mid].last < ucs)
			min = mid + 1;
		else if (combining[mid].first > ucs)
			max = mid - 1;
		else if (combining[mid].first <= ucs && combining[mid].last >= ucs)
			return 0;
	}

	/* if we arrive here, ucs is not a combining or C0/C1 control character */

	/* fast test for majority of non-wide scripts */
	if (ucs < 0x1100)
		return 1;

	return 1 +
		((ucs >= 0x1100 && ucs <= 0x115f) || /* Hangul Jamo */
		 (ucs >= 0x2e80 && ucs <= 0xa4cf && (ucs & ~0x0011) != 0x300a &&
		  ucs != 0x303f) ||                  /* CJK ... Yi */
		 (ucs >= 0xac00 && ucs <= 0xd7a3) || /* Hangul Syllables */
		 (ucs >= 0xf900 && ucs <= 0xfaff) || /* CJK Compatibility Ideographs */
		 (ucs >= 0xfe30 && ucs <= 0xfe6f) || /* CJK Compatibility Forms */
		 (ucs >= 0xff00 && ucs <= 0xff5f) || /* Fullwidth Forms */
		 (ucs >= 0xffe0 && ucs <= 0xffe6));
}

static std::string
truncate_utf8(const std::string& str, int width) {
	std::wstring wstr = utf8_to_wstring(str);
	std::string ret;
	std::wstring::iterator it;
	int total = 0;
	for (it = wstr.begin(); it != wstr.end(); it++) {
		unsigned char bytes[MB_CUR_MAX];
		total += wcwidth_ucs(*it);
		if (total > width) break;
		int len = utf_char2bytes(*it, bytes);
		bytes[len] = 0;
		ret += (char*)bytes;
	}
	for (; total < width; total++)
		ret += " ";
	return ret;
}

static std::string
wrap_utf8(const std::string& str, int width) {
	std::wstring wstr = utf8_to_wstring(str);
	std::string ret;
	std::wstring::iterator it;
	int cells = 0;
	for (it = wstr.begin(); it != wstr.end(); it++) {
		unsigned char bytes[MB_CUR_MAX];
		int cell = wcwidth_ucs(*it);
		cells += cell;
		if (*it == '\n') cells = 0;
		if (cells > width) {
			cells = 0;
			ret += "\n";
		}
		int len = utf_char2bytes(*it, bytes);
		bytes[len] = 0;
		ret += (char*)bytes;
	}
	return ret;
}

static void
fetch_entries(std::string email, std::string password, std::vector<ENTRY>& entries) {
	CURL *curl;
	CURLcode res;
	char error[CURL_ERROR_SIZE];
	char* data;
	MEMFILE* mf;
	char* ptr;
	char* linebreak;
	xmlDocPtr doc = NULL;
	xmlXPathContextPtr xpathctx;
	xmlXPathObjectPtr xpathobj;
	char timebuf[256];

	std::string postdata;
	postdata += "Email=" + email;
	postdata += "&Passwd=" + password;
	postdata += "&source=cpeep";
	postdata += "&service=reader";

	mf = memfopen();
	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, "https://www.google.com/accounts/ClientLogin");
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
	curl_easy_setopt(curl, CURLOPT_POST, 1);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, postdata.size());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, memfwrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, mf);
	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (res != CURLE_OK) {
		fputs(error, stderr);
		memfclose(mf);
		return;
	}
	data = memfstrdup(mf);
	memfclose(mf);
	ptr = strstr(data, "SID=");
	if (!ptr) {
		return;
	}
	linebreak = strpbrk(ptr, "\r\n\t ");
	if (linebreak) *linebreak = 0;
	std::string cookie = ptr;
	free(data);

	mf = memfopen();
	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, "http://www.google.com/reader/api/0/token");
	curl_easy_setopt(curl, CURLOPT_COOKIE, cookie.c_str());
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, memfwrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, mf);
	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (res != CURLE_OK) {
		fputs(error, stderr);
		memfclose(mf);
		return;
	}
	data = memfstrdup(mf);
	memfclose(mf);
	cookie += "; T=";
	cookie += data;
	free(data);

	sprintf(timebuf, "%lu", time(NULL)*1000);
	std::string url = "http://www.google.com/reader/atom/user/-/state/com.google/reading-list?n=50&ck=";
	url += timebuf;
	mf = memfopen();
	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_COOKIE, cookie.c_str());
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, memfwrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, mf);
	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (res != CURLE_OK) {
		fputs(error, stderr);
		memfclose(mf);
		return;
	}
	doc = xmlParseMemory(mf->data, mf->size);
	memfclose(mf);
	xpathctx = doc ? xmlXPathNewContext(doc) : NULL;
	if (xpathctx) xmlXPathRegisterNs(xpathctx, (xmlChar*)"atom", (xmlChar*)"http://www.w3.org/2005/Atom");
	xpathobj = xpathctx ? xmlXPathEvalExpression((xmlChar*) "//atom:entry", xpathctx) : NULL;
	if (xpathobj) {
		int n;
		xmlNodeSetPtr nodes = xpathobj->nodesetval;
		for(n = 0; nodes && n < xmlXPathNodeSetGetLength(nodes); n++) {
			ENTRY entry;
			xmlNodePtr node = nodes->nodeTab[n]->children;
			while (node) {
				if (!strcmp((char*)node->name, "title"))
					entry.title = (char*)node->children->content;
				if (!strcmp((char*)node->name, "summary") || !strcmp((char*)node->name, "content"))
					entry.content = (char*) node->children->content;
				if (!strcmp((char*)node->name, "link") && !strcmp("alternate", (char*)xmlGetProp(node, (xmlChar*)"rel")))
					entry.url = (char*) xmlGetProp(node, (xmlChar*)"href");
				if (!strcmp((char*)node->name, "author")) {
					xmlNodePtr author = node->children;
					while (author) {
						if (!strcmp((char*)author->name, "name"))
							entry.author = (char*)author->children->content;
						author = author->next;
					}
				}
				if (!strcmp((char*)node->name, "source")) {
					xmlNodePtr source = node->children;
					while (source) {
						if (!strcmp((char*)source->name, "title"))
							entry.source = (char*)source->children->content;
						source = source->next;
					}
				}
				node = node->next;
			}
			entries.push_back(entry);
		}
	}
	if (xpathobj) xmlXPathFreeObject(xpathobj);
	if (xpathctx) xmlXPathFreeContext(xpathctx);
	if (doc) xmlFreeDoc(doc);

	return;
}

void paint_item(ENTRY& entry, int pos, bool highlight) {
	std::string source = html_decode(entry.source);
	std::string title = html_decode(entry.title);
	source = truncate_utf8(source, 20);
	title = truncate_utf8(title, COLS - 20 - 2);
	if (highlight) attron(A_BOLD);
	move(pos, 0);
	addstr(source.c_str());
	addstr(" ");
	addstr(title.c_str());
	addstr(" ");
	attroff(A_BOLD);
}

void paint_items(std::vector<ENTRY>& entries, int pos, int row) {
	nonl();
	clear();
	for (int n = pos; n < entries.size() && n < pos + LINES; n++)
		paint_item(entries[n], n - pos, n == row);
	refresh();
}

void paint_content(ENTRY& entry, std::vector<std::string>& lines, int pos) {
	nl();
	clear();
	attron(A_BOLD);
	mvaddstr(0, 0, html_decode(entry.source).c_str());
	mvaddstr(1, 0, html_decode(entry.title).c_str());
	mvaddstr(2, 0, html_decode(entry.author).c_str());
	attroff(A_BOLD);
	move(3, 0);
	hline(ACS_HLINE, COLS);
	for (int n = 4; n < LINES; n++) {
		move(n, 0);
		if (n + pos - 4 < lines.size()) addstr(lines[n - 4 + pos].c_str());
	}
	refresh();
}

int  opterr = 1;
int  optind = 1;
int  optopt;
char *optarg;
 
static int getopt(int argc, char** argv, const char* opts) {
	static int sp = 1;
	register int c;
	register char *cp;

	if(sp == 1) {
		if(optind >= argc ||
				argv[optind][0] != '-' || argv[optind][1] == '\0')
			return(EOF);
		else if(strcmp(argv[optind], "--") == 0) {
			optind++;
			return(EOF);
		}
	}
	optopt = c = argv[optind][sp];
	if(c == ':' || (cp=strchr((char*)opts, c)) == NULL) {
		if(argv[optind][++sp] == '\0') {
			optind++;
			sp = 1;
		}
		return('?');
	}
	if(*++cp == ':') {
		if(argv[optind][sp+1] != '\0')
			optarg = &argv[optind++][sp+1];
		else if(++optind >= argc) {
			sp = 1;
			return('?');
		} else
			optarg = argv[optind++];
		sp = 1;
	} else {
		if(argv[optind][++sp] == '\0') {
			sp = 1;
			optind++;
		}
		optarg = NULL;
	}
	return(c);
}

typedef std::map<std::string, std::string>	Config;
typedef std::map<std::string, Config>		ConfigList;
ConfigList loadConfigs(const char* filename) {
	ConfigList configs;
	Config config;
	char buffer[BUFSIZ];
	FILE* fp = fopen(filename, "r");
	std::string profile = "global";
	while(fp && fgets(buffer, sizeof(buffer), fp)) {
		char* line = buffer;
		char* ptr = strpbrk(line, "\r\n");
		if (ptr) *ptr = 0;
		ptr = strchr(line, ']');
		if (*line == '[' && ptr) {
			*ptr = 0;
			if (config.size())
				configs[profile] = config;
			config.clear();
			profile = line+1;
			continue;
		}
		ptr = strchr(line, '=');
		if (ptr && *line != ';') {
			*ptr++ = 0;
			config[line] = ptr;
		}
	}
	configs[profile] = config;
	if (fp) fclose(fp);
	return configs;
}

int main(int argc, char* argv[]) {
	std::vector<ENTRY> entries;
	int c;
	char* cfg = NULL;

	opterr = 0;
	while ((c = getopt(argc, (char**)argv, "p:c:d:v") != -1)) {
		switch (optopt) {
		case 'c': cfg = optarg; break;
		case '?': break;
		default:
			argc = 0;
			break;
		}
		optarg = NULL;
	}
	ConfigList configs;
	if (cfg) configs = loadConfigs(cfg);
	entries.clear();
	fetch_entries(configs["global"]["user"], configs["global"]["pass"], entries);

	setlocale(LC_ALL, "");
	initscr();
	keypad(stdscr, TRUE);
	cbreak();
	noecho();
	scrollok(stdscr, false);
	curs_set(0);

	int row = 0, list_scroll = 0, view_scroll = 0;
	bool loop = true;
	bool view = false;
	std::vector<std::string> content_lines;
	paint_items(entries, list_scroll, row);
	while (loop) {
		switch (getch()) {
			case 'o':
				view = true;
				view_scroll = 0;
				if (row < entries.size()) {
					std::string str = entries[row].content;
					std::string::size_type n1, n2, nb = 0;
					while((n1 = str.find("<", nb)) != std::string::npos
					   && (n2 = str.find(">", n1)) != std::string::npos) {
						bool linebreak = false;
						if (!strncmp(str.c_str() + n1, "<br", 3)
						 || !strncmp(str.c_str() + n1, "<p", 2)
						 || !strncmp(str.c_str() + n1, "</p", 3)
						 )
							str.replace(n1, n2-n1+1, "\n");
						else
							str.erase(n1, n2-n1+1);
						nb = n1;
					}
					str = html_decode(str);
					replace_string(str, "\r", "");
					replace_string(str, "\t", "  ");
					replace_string(str, "\n  ", "\n ");
					replace_string(str, "\n \n", "\n\n");
					replace_string(str, "\n\n\n", "\n\n");
					str = wrap_utf8(str, COLS);
					content_lines = split_string(str, "\n");
					paint_content(entries[row], content_lines, 0);
				}
				break;
			case 'q':
				if (!view) loop = false;
				else {
					view = false;
					paint_items(entries, list_scroll, row);
				}
				break;
			case 'v':
				if (view && row < entries.size()) {
#ifdef _WIN32
					ShellExecute(NULL, "open", entries[row].url.c_str(), NULL, NULL, SW_SHOW);
#else
					std::string command = "firefox \"";
					command += entries[row].url;
					command += "\" 2>&1 > /dev/null &";
					system(command.c_str());
#endif
				}
				break;
			case 'k':
				if (view) {
					if (view_scroll > 0) view_scroll--;
					paint_content(entries[row], content_lines, view_scroll);
				} else {
					if (row > 0) {
						row--;
						if (row < list_scroll) list_scroll--;
						paint_items(entries, list_scroll, row);
					}
				}
				continue;
			case 'j':
				if (view) {
					if (view_scroll + LINES < content_lines.size()) view_scroll++;
					paint_content(entries[row], content_lines, view_scroll);
				} else {
					if (row + 1 < entries.size()) {
						row++;
						if (row > list_scroll + LINES - 1) list_scroll++;
						paint_items(entries, list_scroll, row);
					}
				}
				continue;
		}
	}

	curs_set(1);
	move(LINES-1, 0);
	clear();
	refresh();
	endwin();
	return 0;
}
