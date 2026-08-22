#ifndef USER_INTERFACE_H
#define USER_INTERFACE_H

class UserInterface
{
  public:
    virtual int run() = 0;

    virtual ~UserInterface() = default;
};

#endif
