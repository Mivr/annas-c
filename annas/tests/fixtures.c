/* annas/tests/fixtures.c: the recorded live captures of fixtures/live/ (byte for byte), embedded because c_test
 * targets carry no data files. */
#include "annas/tests/kit.h"

const char kit_fixture_401[] =
    "{\n"
    "  \"///download_url\": [\n"
    "    \"This API is intended as a stable JSON API for getting fast download files as a member.\",\n"
    "    \"A successful request will return status code 200 or 204, a `download_url` field and `account_fast_download_info`.\",\n"
    "    \"Bad responses use different status codes, a `download_url` set to `null`, and `error` field with string description.\",\n"
    "    \"Accepted query parameters:\",\n"
    "    \"- `md5` (required): the md5 string of the requested file.\",\n"
    "    \"- `key` (required): the secret key for your account (which must have membership).\",\n"
    "    \"- `path_index` (optional): Integer, 0 or larger, indicating the collection (if the file is present in more than one).\",\n"
    "    \"- `domain_index` (optional): Integer, 0 or larger, indicating the download server, e.g. 0='Fast Partner Server #1'.\",\n"
    "    \"These parameters correspond to the fast download page like this: /fast_download/{md5}/{path_index}/{domain_index}\",\n"
    "    \"Example: /dyn/api/fast_download.json?md5=d6e1dc51a50726f00ec438af21952a45&key=YOUR_SECRET_KEY\"\n"
    "  ],\n"
    "  \"download_url\": null,\n"
    "  \"error\": \"Invalid secret key\"\n"
    "}";

const char kit_fixture_404[] =
    "{\n"
    "  \"///download_url\": [\n"
    "    \"This API is intended as a stable JSON API for getting fast download files as a member.\",\n"
    "    \"A successful request will return status code 200 or 204, a `download_url` field and `account_fast_download_info`.\",\n"
    "    \"Bad responses use different status codes, a `download_url` set to `null`, and `error` field with string description.\",\n"
    "    \"Accepted query parameters:\",\n"
    "    \"- `md5` (required): the md5 string of the requested file.\",\n"
    "    \"- `key` (required): the secret key for your account (which must have membership).\",\n"
    "    \"- `path_index` (optional): Integer, 0 or larger, indicating the collection (if the file is present in more than one).\",\n"
    "    \"- `domain_index` (optional): Integer, 0 or larger, indicating the download server, e.g. 0='Fast Partner Server #1'.\",\n"
    "    \"These parameters correspond to the fast download page like this: /fast_download/{md5}/{path_index}/{domain_index}\",\n"
    "    \"Example: /dyn/api/fast_download.json?md5=d6e1dc51a50726f00ec438af21952a45&key=YOUR_SECRET_KEY\"\n"
    "  ],\n"
    "  \"download_url\": null,\n"
    "  \"error\": \"Record not found\"\n"
    "}";

const char kit_fixture_302[] =
    "<!doctype html>\n"
    "<html lang=en>\n"
    "<title>Redirecting...</title>\n"
    "<h1>Redirecting...</h1>\n"
    "<p>You should be redirected automatically to the target URL: <a href=\"/search?q=lakos&amp;content=book_any&amp;check=1\">/search?q=lakos&amp;content=book_any&amp;check=1</a>. If not, click the link.\n";

