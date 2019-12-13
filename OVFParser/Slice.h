#pragma once
//minor specifications for *abstractly* defining a slice

namespace VField{
    //special points
    enum class slice_pnt {begin, end};
    template<typename T>
    class slice_pos{
    private:
        slice_pnt special {slice_pnt::begin};
        T pos{};
        bool isSpecial_ {true};
    public:
        constexpr slice_pos() noexcept;
        constexpr slice_pos(const T& v) noexcept: pos(v), isSpecial_(false) {}
        constexpr slice_pos(const slice_pnt& v) noexcept: special(v), isSpecial_(true) {}
        //and conversions
        bool operator== (const slice_pos& ref) const noexcept
        {
            if(isSpecial_ != ref.isSpecial_)
                return false;
            if(isSpecial_)
                return special == ref.special;
            else
                return pos == ref.pos;
        }
        inline bool operator!= (const slice_pos& ref) const noexcept
        { return !(*this == ref); }
        //check if position is special
        inline bool isSpecial() const noexcept
        { return isSpecial_;}
        //return the value
        T getPos() const noexcept
        { if(isSpecial_) return 0; return  pos; }
    };
    template<typename T>
    struct slice{
        using pos_type = slice_pos<T>;
        pos_type begin {slice_pnt::begin};
        pos_type end {slice_pnt::end};
        T stride {1};

        //constructors
        constexpr slice( const pos_type& begin_ = slice_pnt::begin,
                             const pos_type& end_   = slice_pnt::end,
                             T& stride_ = 1 ) noexcept: begin(begin_), end(end_), stride(stride_)
        {}
    };
}
