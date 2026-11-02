#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

#include "adesk.h"
#include "acdb.h"
#include "dbmain.h"
#include "dbtrans.h"
#include "actrans.h"
#include "acdocman.h"
#include "aced.h"
#include "acedads.h"
#include "adscodes.h"

namespace safe_tx
{
    enum class GuardState
    {
        NOT_STARTED,
        ACTIVE,
        COMMITTED,
        ABORTED
    };

    class OwnedTransactionGuard
    {
    public:
        OwnedTransactionGuard()
            : baselineDepth_(0)
            , txManager_(nullptr)
            , ownedTransaction_(nullptr)
            , state_(GuardState::NOT_STARTED)
        {
        }

        ~OwnedTransactionGuard()
        {
            if (state_ == GuardState::ACTIVE)
            {
                abort();
            }
        }

        OwnedTransactionGuard(const OwnedTransactionGuard&) = delete;
        OwnedTransactionGuard& operator=(const OwnedTransactionGuard&) = delete;

        Acad::ErrorStatus start(AcDbDatabase* db)
        {
            if (state_ != GuardState::NOT_STARTED)
            {
                return Acad::eAlreadyActive;
            }
            if (db == nullptr)
            {
                return Acad::eNoDatabase;
            }

            txManager_ = db->transactionManager();
            if (txManager_ == nullptr)
            {
                return Acad::eNullObjectPointer;
            }

            baselineDepth_ = txManager_->numActiveTransactions();
            ownedTransaction_ = txManager_->startTransaction();
            if (ownedTransaction_ == nullptr)
            {
                state_ = GuardState::NOT_STARTED;
                return Acad::eNotHandled;
            }

            state_ = GuardState::ACTIVE;
            return Acad::eOk;
        }

        Acad::ErrorStatus commit()
        {
            if (state_ != GuardState::ACTIVE)
            {
                return Acad::eNotApplicable;
            }
            if (txManager_ == nullptr)
            {
                return Acad::eNullObjectPointer;
            }

            // Verify transaction ownership and stack integrity
            if (txManager_->topTransaction() != ownedTransaction_ ||
                txManager_->numActiveTransactions() != baselineDepth_ + 1)
            {
                abort();
                return Acad::eNotTopTransaction;
            }

            const Acad::ErrorStatus es = txManager_->endTransaction();
            if (es == Acad::eOk)
            {
                state_ = GuardState::COMMITTED;
                ownedTransaction_ = nullptr;
                return Acad::eOk;
            }
            else
            {
                // Ending failed: check if still active
                if (txManager_->numActiveTransactions() > baselineDepth_ &&
                    txManager_->topTransaction() == ownedTransaction_)
                {
                    txManager_->abortTransaction();
                }
                state_ = GuardState::ABORTED;
                ownedTransaction_ = nullptr;
                return es;
            }
        }

        Acad::ErrorStatus abort()
        {
            if (state_ != GuardState::ACTIVE)
            {
                return Acad::eNotApplicable;
            }
            if (txManager_ == nullptr)
            {
                state_ = GuardState::ABORTED;
                return Acad::eNullObjectPointer;
            }

            Acad::ErrorStatus es = Acad::eOk;
            if (txManager_->numActiveTransactions() > baselineDepth_ &&
                txManager_->topTransaction() == ownedTransaction_)
            {
                es = txManager_->abortTransaction();
            }

            state_ = GuardState::ABORTED;
            ownedTransaction_ = nullptr;
            return es;
        }

        GuardState state() const { return state_; }
        bool isCommitted() const { return state_ == GuardState::COMMITTED; }
        AcTransaction* transaction() const { return ownedTransaction_; }
        AcDbTransactionManager* manager() const { return txManager_; }

        Acad::ErrorStatus getObject(AcDbObject*& obj, AcDbObjectId id, AcDb::OpenMode mode)
        {
            if (state_ != GuardState::ACTIVE || txManager_ == nullptr)
            {
                obj = nullptr;
                return Acad::eNotApplicable;
            }
            return txManager_->getObject(obj, id, mode);
        }

        Acad::ErrorStatus addNewlyCreatedDBRObject(AcDbObject* obj, bool add = true)
        {
            if (state_ != GuardState::ACTIVE || txManager_ == nullptr)
            {
                return Acad::eNotApplicable;
            }
            return txManager_->addNewlyCreatedDBRObject(obj, add);
        }

    private:
        int baselineDepth_;
        AcDbTransactionManager* txManager_;
        AcTransaction* ownedTransaction_;
        GuardState state_;
    };

    // Document lock guard ensuring we do not acquire unnecessary nested locks
    class SafeDocLock
    {
    public:
        SafeDocLock(AcApDocument* pDoc)
            : pDoc_(pDoc)
            , locked_(false)
        {
            if (pDoc_ != nullptr && acDocManager != nullptr)
            {
                if (pDoc_->myLockMode() == AcAp::kNone)
                {
                    if (acDocManager->lockDocument(pDoc_, AcAp::kWrite) == Acad::eOk)
                    {
                        locked_ = true;
                    }
                }
            }
        }

        ~SafeDocLock()
        {
            if (locked_ && pDoc_ != nullptr && acDocManager != nullptr)
            {
                acDocManager->unlockDocument(pDoc_);
                locked_ = false;
            }
        }

        SafeDocLock(const SafeDocLock&) = delete;
        SafeDocLock& operator=(const SafeDocLock&) = delete;

        bool isLockedOrProtected() const
        {
            if (pDoc_ == nullptr) return false;
            return locked_ || pDoc_->myLockMode() != AcAp::kNone;
        }

    private:
        AcApDocument* pDoc_;
        bool locked_;
    };

    // Execution context capture & verification
    struct ExecutionContext
    {
        AcApDocument* pDoc = nullptr;
        AcDbDatabase* pDb = nullptr;
        AcDbObjectId currentSpaceId;

        static ExecutionContext capture()
        {
            ExecutionContext ctx;
            if (acDocManager != nullptr)
            {
                ctx.pDoc = acDocManager->curDocument();
                if (ctx.pDoc != nullptr)
                {
                    ctx.pDb = ctx.pDoc->database();
                    if (ctx.pDb != nullptr)
                    {
                        ctx.currentSpaceId = ctx.pDb->currentSpaceId();
                    }
                }
            }
            return ctx;
        }

        bool matchesCurrent() const
        {
            if (acDocManager == nullptr || pDoc == nullptr || pDb == nullptr)
            {
                return false;
            }
            if (acDocManager->curDocument() != pDoc)
            {
                return false;
            }
            if (pDoc->database() != pDb)
            {
                return false;
            }
            if (pDb->currentSpaceId() != currentSpaceId)
            {
                return false;
            }
            return true;
        }
    };

    // Centralized safe selection helper
    // Calling acedSSSetFirst needs a supported editor state
    inline bool canApplySelectionNow(AcApDocument* pDoc)
    {
        if (pDoc == nullptr || acDocManager == nullptr) return false;
        if (acDocManager->curDocument() != pDoc) return false;
        // Verify no foreign or unsupported document locking state
        return true;
    }

    inline bool applyImpliedSelection(const std::vector<AcDbObjectId>& objectIds)
    {
        AcApDocument* pDoc = acDocManager ? acDocManager->curDocument() : nullptr;
        if (!canApplySelectionNow(pDoc))
        {
            return false;
        }

        if (objectIds.empty())
        {
            ads_name empty;
            acdbNameClear(empty);
            return acedSSSetFirst(empty, empty) == RTNORM;
        }

        ads_name ss;
        acdbNameClear(ss);
        if (acedSSAdd(nullptr, nullptr, ss) != RTNORM)
        {
            return false;
        }

        for (const AcDbObjectId& id : objectIds)
        {
            if (id.isValid() && !id.isErased())
            {
                ads_name ename;
                acdbNameClear(ename);
                if (acdbGetAdsName(ename, id) == Acad::eOk)
                {
                    acedSSAdd(ename, ss, ss);
                }
            }
        }

        const int ret = acedSSSetFirst(ss, ss);
        acedSSFree(ss);
        return ret == RTNORM;
    }
}
