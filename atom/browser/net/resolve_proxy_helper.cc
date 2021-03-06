// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/net/resolve_proxy_helper.h"

#include <utility>

#include "atom/browser/atom_browser_context.h"
#include "base/bind.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "net/proxy_resolution/proxy_info.h"
#include "services/network/public/mojom/network_context.mojom.h"

using content::BrowserThread;

namespace atom {

ResolveProxyHelper::ResolveProxyHelper(AtomBrowserContext* browser_context)
    : binding_(this), browser_context_(browser_context) {}

ResolveProxyHelper::~ResolveProxyHelper() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(!owned_self_);
  DCHECK(!binding_.is_bound());
  // Clear all pending requests if the ProxyService is still alive.
  pending_requests_.clear();
}

void ResolveProxyHelper::ResolveProxy(const GURL& url,
                                      const ResolveProxyCallback& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // Enqueue the pending request.
  pending_requests_.push_back(PendingRequest(url, callback));

  // If nothing is in progress, start.
  if (!binding_.is_bound()) {
    DCHECK_EQ(1u, pending_requests_.size());
    StartPendingRequest();
  }
}

void ResolveProxyHelper::StartPendingRequest() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(!binding_.is_bound());
  DCHECK(!pending_requests_.empty());

  // Start the request.
  network::mojom::ProxyLookupClientPtr proxy_lookup_client;
  binding_.Bind(mojo::MakeRequest(&proxy_lookup_client));
  binding_.set_connection_error_handler(
      base::BindOnce(&ResolveProxyHelper::OnProxyLookupComplete,
                     base::Unretained(this), net::ERR_ABORTED, base::nullopt));
  content::BrowserContext::GetDefaultStoragePartition(browser_context_)
      ->GetNetworkContext()
      ->LookUpProxyForURL(pending_requests_.front().url,
                          std::move(proxy_lookup_client));
}

void ResolveProxyHelper::OnProxyLookupComplete(
    int32_t net_error,
    const base::Optional<net::ProxyInfo>& proxy_info) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(!pending_requests_.empty());

  binding_.Close();

  // Clear the current (completed) request.
  PendingRequest completed_request = std::move(pending_requests_.front());
  pending_requests_.pop_front();

  std::string proxy;
  if (proxy_info)
    proxy = proxy_info->ToPacString();

  if (!completed_request.callback.is_null())
    completed_request.callback.Run(proxy);

  // Start the next request.
  if (!pending_requests_.empty())
    StartPendingRequest();
}

ResolveProxyHelper::PendingRequest::PendingRequest(
    const GURL& url,
    const ResolveProxyCallback& callback)
    : url(url), callback(callback) {}

ResolveProxyHelper::PendingRequest::PendingRequest(
    ResolveProxyHelper::PendingRequest&& pending_request) = default;

ResolveProxyHelper::PendingRequest::~PendingRequest() noexcept = default;

ResolveProxyHelper::PendingRequest& ResolveProxyHelper::PendingRequest::
operator=(ResolveProxyHelper::PendingRequest&& pending_request) noexcept =
    default;

}  // namespace atom
