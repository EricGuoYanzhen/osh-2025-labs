#include <string>
#include <iostream>
#include <vector>
#include <sstream>
#include <deque>

std::vector<std::string> split(const std::string& str, const std::string& delimiter) {
    std::vector<std::string> internal;
    std::stringstream ss(str); // Turn the string into a stream.
    std::string tok;
    
    std::string::size_type pos = 0;
    while (pos < str.size()) {
        std::string::size_type next_pos = str.find(delimiter, pos);
        if (next_pos == std::string::npos) {
            next_pos = str.size();
        }
        internal.push_back(str.substr(pos, next_pos - pos));
        pos = next_pos + delimiter.size();
    }

    return internal;
}

int main()
{
    std::string str = "I  love  OSH  2025";
    std::vector<std::string> v = split(str, "  ");
    for (std::string s : v) {
        std::cout << s << std::endl;
    }

    return 0;
}