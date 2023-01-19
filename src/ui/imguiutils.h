#ifndef IMGUIUTILS_H
#define IMGUIUTILS_H

inline ImVec2 operator+(const ImVec2 a, const ImVec2 b) {
    ImVec2 r = a;
    r.x += b.x;
    r.y += b.y;
    return r;
}

inline ImVec2 operator-(const ImVec2 a, const ImVec2 b) {
    ImVec2 r = a;
    r.x -= b.x;
    r.y -= b.y;
    return r;
}

#endif
