#ifndef RECOVERABLE_HPP
#define RECOVERABLE_HPP

// TODO: report recover errors/exceptions

class Recoverable{
public:
    // return num of blocks recovered.
    virtual int recover(bool simulated = false) = 0;
};

#endif