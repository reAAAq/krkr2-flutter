//
// Created by LiDon on 2025/9/15.
//
#pragma once

#include "tjs.h"

namespace motion {

    class SeparateLayerAdaptor {
    public:
        explicit SeparateLayerAdaptor(iTJSDispatch2 *owner = nullptr)
            : _owner(owner) {
            if(_owner) {
                _owner->AddRef();
            }
        }

        ~SeparateLayerAdaptor() {
            if(_target) {
                _target->Release();
                _target = nullptr;
            }
            if(_owner) {
                _owner->Release();
                _owner = nullptr;
            }
        }

        SeparateLayerAdaptor(const SeparateLayerAdaptor &) = delete;
        SeparateLayerAdaptor &operator=(const SeparateLayerAdaptor &) = delete;

        iTJSDispatch2 *getOwner() const { return _owner; }
        iTJSDispatch2 *getTarget() const { return _target; }
        void setTarget(iTJSDispatch2 *target) {
            if(target) {
                target->AddRef();
            }
            if(_target) {
                _target->Release();
            }
            _target = target;
        }

    private:
        iTJSDispatch2 *_owner = nullptr;
        iTJSDispatch2 *_target = nullptr;
    };
} // namespace motion

/** When \a base is a SeparateLayerAdaptor, returns its underlying Layer (with layerTreeOwnerInterface).
 *  Otherwise returns \a base. Used so Layer constructor receives an owner that has layerTreeOwnerInterface. */
iTJSDispatch2 *ResolveLayerTreeOwnerBase(iTJSDispatch2 *base);
