// NOTE: don't include this file elsewhere!
// this is supposed to be a part of EpochSys.hpp

// TODO: replace `new` operator of T with
// per-heap allocation and placement new.

template<typename T>
T* pnew(){
    T* ret = new T();
    register_alloc_pblk(ret, epochs[tid].ui);
    return ret;
}

template<typename T, typename T1>
T* pnew(T1 a1){
    T* ret = new T(a1);
    register_alloc_pblk(ret, epochs[tid].ui);
    return ret;
}

template<typename T, typename T1, typename T2>
T* pnew(T1 a1, T2 a2){
    T* ret = new T(a1, a2);
    register_alloc_pblk(ret, epochs[tid].ui);
    return ret;
}

template<typename T, typename T1, typename T2, typename T3>
T* pnew(T1 a1, T2 a2, T3 a3){
    T* ret = new T(a1, a2, a3);
    register_alloc_pblk(ret, epochs[tid].ui);
    return ret;
}

template<typename T, typename T1, typename T2, typename T3, typename T4>
T* pnew(T1 a1, T2 a2, T3 a3, T4 a4){
    T* ret = new T(a1, a2, a3, a4);
    register_alloc_pblk(ret, epochs[tid].ui);
    return ret;
}

template<typename T, typename T1, typename T2, typename T3, typename T4, typename T5>
T* pnew(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5){
    T* ret = new T(a1, a2, a3, a4, a5);
    register_alloc_pblk(ret, epochs[tid].ui);
    return ret;
}

template<typename T, typename T1, typename T2, typename T3, typename T4, typename T5, 
            typename T6>
T* pnew(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6){
    T* ret = new T(a1, a2, a3, a4, a5, a6);
    register_alloc_pblk(ret, epochs[tid].ui);
    return ret;
}

template<typename T, typename T1, typename T2, typename T3, typename T4, typename T5, 
            typename T6, typename T7>
T* pnew(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7){
    T* ret = new T(a1, a2, a3, a4, a5, a6, a7);
    register_alloc_pblk(ret, epochs[tid].ui);
    return ret;
}

template<typename T, typename T1, typename T2, typename T3, typename T4, typename T5, 
            typename T6, typename T7, typename T8>
T* pnew(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8){
    T* ret = new T(a1, a2, a3, a4, a5, a6, a7, a8);
    register_alloc_pblk(ret, epochs[tid].ui);
    return ret;
}

template<typename T, typename T1, typename T2, typename T3, typename T4, typename T5, 
            typename T6, typename T7, typename T8, typename T9>
T* pnew(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9){
    T* ret = new T(a1, a2, a3, a4, a5, a6, a7, a8, a9);
    register_alloc_pblk(ret, epochs[tid].ui);
    return ret;
}

template<typename T, typename T1, typename T2, typename T3, typename T4, typename T5, 
            typename T6, typename T7, typename T8, typename T9, typename T10>
T* pnew(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10){
    T* ret = new T(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
    register_alloc_pblk(ret, epochs[tid].ui);
    return ret;
}

// add more as needed.