// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Ge,Jun (gejun@baidu.com)

#include <gflags/gflags.h>
#include "brpc/reloadable_flags.h"
#include "brpc/load_balancer.h"


namespace brpc {

DEFINE_bool(show_lb_in_vars, false, "Describe LoadBalancers in vars");
BRPC_VALIDATE_GFLAG(show_lb_in_vars, PassValidate);

// For assigning unique names for lb.
static butil::static_atomic<int> g_lb_counter = BUTIL_STATIC_ATOMIC_INIT(0);

void SharedLoadBalancer::DescribeLB(std::ostream& os, void* arg) {
    (static_cast<SharedLoadBalancer*>(arg))->Describe(os, DescribeOptions());
}

void SharedLoadBalancer::ExposeLB() {
    bool changed = false;
    _st_mutex.lock();
    if (!_exposed) {
        _exposed = true;
        changed = true;
    }
    _st_mutex.unlock();
    if (changed) {
        char name[32];
        snprintf(name, sizeof(name), "_load_balancer_%d", g_lb_counter.fetch_add(
                     1, butil::memory_order_relaxed));
        _st.expose(name);
    }
}

SharedLoadBalancer::SharedLoadBalancer()
    : _lb(NULL)
    , _weight_sum(0)
    , _exposed(false)
    , _st(DescribeLB, this) {
}

SharedLoadBalancer::~SharedLoadBalancer() {
    _st.hide();
    if (_lb) {
        _lb->Destroy();
        _lb = NULL;
    }
}

int SharedLoadBalancer::Init(const char* lb_protocol) {
    std::string lb_name;
    butil::StringPairs lb_params;
    if (!ParseParameters(lb_protocol, &lb_name, &lb_params)) {
        LOG(FATAL) << "Fail to parse this load balancer protocol '" << lb_protocol << '\'';
        return -1;
    }
    const LoadBalancer* lb = LoadBalancerExtension()->Find(lb_name.c_str());
    if (lb == NULL) {
        LOG(FATAL) << "Fail to find LoadBalancer by `" << lb_name << "'";
        return -1;
    }
    LoadBalancer* lb_copy = lb->New();
    if (lb_copy == NULL) {
        LOG(FATAL) << "Fail to new LoadBalancer";
        return -1;
    }
    _lb = lb_copy;
    if (!_lb->SetParameters(lb_params)) {
        LOG(FATAL) << "Fail to set parameters of lb `" << lb_protocol << "'";
        return -1;
    }
    if (FLAGS_show_lb_in_vars && !_exposed) {
        ExposeLB();
    }
    return 0;
}

void SharedLoadBalancer::Describe(std::ostream& os,
                                  const DescribeOptions& options) {
    if (_lb == NULL) {
        os << "lb=NULL";
    } else {
        _lb->Describe(os, options);
    }
}

bool SharedLoadBalancer::ParseParameters(const butil::StringPiece& lb_protocol,
                                         std::string* lb_name,
                                         butil::StringPairs* lb_params) {
    lb_name->clear();
    lb_params->clear();
    if (lb_protocol.empty()) {
        return false;
    }
    size_t pos = lb_protocol.find(':');
    if (pos == std::string::npos) {
        lb_name->append(lb_protocol.data(), lb_protocol.size());
    } else {
        lb_name->append(lb_protocol.data(), pos);
        butil::StringPiece params_piece = lb_protocol.substr(pos + sizeof(':'));
        std::string params_str(params_piece.data(), params_piece.size());
        if (!butil::SplitStringIntoKeyValuePairs(params_str, '=', ' ', lb_params)) {
            lb_params->clear();
            return false;
        }
    }

    return true;
}
																				 
} // namespace brpc
