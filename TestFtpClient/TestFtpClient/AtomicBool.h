#ifndef _AtomicBool_H_
#define _AtomicBool_H_

#include <Poco/Thread.h>

class AtomicBool
{
public:
    AtomicBool()
    : m_bValue(false){}

    explicit AtomicBool(bool bValue)
    : m_bValue(bValue){}

    void Store(bool bValue)
    {
        Poco::FastMutex::ScopedLock l(m_Muxtex);
        m_bValue = bValue;
    }

    bool Load()
    {
        Poco::FastMutex::ScopedLock l(m_Muxtex);
        return m_bValue;
    }

    bool CompareExchange(bool &bExpected, bool bDesired)
    {
        Poco::FastMutex::ScopedLock l(m_Muxtex);
        if (m_bValue == bExpected)
        {
            bExpected = m_bValue;
            m_bValue = bDesired;
            return true;
        }
        bExpected = m_bValue;
        return false;
    }

private:
    AtomicBool (const AtomicBool &rhs);

    AtomicBool & operator=(const AtomicBool &rhs);

private:
    bool m_bValue;
    Poco::FastMutex m_Muxtex;
};

#endif // _AtomicBool_H_