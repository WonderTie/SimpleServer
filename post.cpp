#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>

using namespace std;

int main() {
    string length = getenv("CONTENT_LENGTH");
    if (!length.empty()) {
        int len = stoi(length);
        char* buffer = new char[len + 1];
        cin.read(buffer, len);
        buffer[len] = '\0';
        cout << "Content-type:text/html\n\n";
        cout << "<html>\n";
        cout << "<head>\n";
        cout << "<title>POST</title>\n";
        cout << "</head>\n";
        cout << "<body>\n";
        cout << "<h2> Your POST data: </h2>\n";
        cout << "<ul>\n";
        char* pch = strtok(buffer, "&");
        while (pch != NULL) {
            cout << "<li>" << pch << "</li>\n";
            pch = strtok(NULL, "&");
        }
        cout << "</ul>\n";
        cout << "</body>\n";
        cout << "</html>\n";
        delete[] buffer;
    } else {
        cout << "Content-type:text/html\n\n";
        cout << "no found\n";
    }
    return 0;
}
