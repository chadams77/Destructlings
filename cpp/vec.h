class vec2 {
public:
    double x, y;
    inline vec2(double _x=0., double _y=0.) { x=_x; y=_y; }
    inline vec2(double _x) { x=_x; y=_x; }
    inline vec2(const vec2 & b) { x=b.x; y=b.y; }
    inline vec2 operator+(const vec2 & b) const { return vec2(x+b.x, y+b.y); }
    inline vec2 operator-(const vec2 & b) const { return vec2(x-b.x, y-b.y); }
    inline vec2 operator+(const double & b) const { return vec2(x+b, y+b); }
    inline vec2 operator-(const double & b) const { return vec2(x-b, y-b); }
    inline vec2 operator*(const vec2 & b) const { return vec2(x*b.x, y*b.y); }
    inline vec2 operator/(const vec2 & b) const { return vec2(x/b.x, y/b.y); }
    inline vec2 operator*(const double & b) const { return vec2(x*b, y*b); }
    inline vec2 operator/(const double & b) const { return vec2(x/b, y/b); }
    inline vec2 & operator+=(const vec2 & b) { x += b.x; y += b.y; return *this; }
    inline vec2 & operator-=(const vec2 & b) { x -= b.x; y -= b.y; return *this; }
    inline vec2 & operator+=(const double & b) { x += b; y += b; return *this; }
    inline vec2 & operator-=(const double & b) { x -= b; y -= b; return *this; }
    inline vec2 & operator*=(const vec2 & b) { x *= b.x; y *= b.y; return *this; }
    inline vec2 & operator/=(const vec2 & b) { x /= b.x; y /= b.y; return *this; }
    inline vec2 & operator*=(const double & b) { x *= b; y *= b; return *this; }
    inline vec2 & operator/=(const double & b) { x /= b; y /= b; return *this; }
    inline double length() const { return sqrt(x*x+y*y); }
    inline double distance(const vec2 & b) const { return (b - (*this)).length(); }
    inline bool operator<(const vec2 & b) const { 
        if (x < b.x) {
            return true;
        }
        else if (x > b.x) {
            return false;
        }
        else {
            return y < b.y;
        }
    }
};

uint32_t max(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

uint32_t min(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}