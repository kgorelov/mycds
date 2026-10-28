#pragma once

#include "metadata/cddb_disc_id.hpp"
#include "metadata/metadata_provider.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mycds {

struct CddbClientConfig {
    std::string endpointUrl{"http://gnudb.gnudb.org/~cddb/cddb.cgi"};
    std::string helloUser{"anonymous"};
    std::string helloHost{"mycds.local"};
    std::string clientName{"libcddb"};
    std::string clientVersion{"1.3.2"};
    int protocolLevel{6};
    long connectTimeoutMs{5000};
    long requestTimeoutMs{10000};
};

class CddbHttpTransport {
public:
    virtual ~CddbHttpTransport() = default;

    virtual std::string performGet(
        const std::string& url,
        long connect_timeout_ms,
        long request_timeout_ms) = 0;
};

class CurlCddbHttpTransport final : public CddbHttpTransport {
public:
    std::string performGet(
        const std::string& url,
        long connect_timeout_ms,
        long request_timeout_ms) override;
};

std::pair<std::string, std::string> split_cddb_artist_title(std::string_view dtitle);

std::string build_cddb_cgi_url(
    const CddbClientConfig& config,
    std::string_view cddb_command);

std::vector<MetadataCandidate> parse_cddb_query_response(std::string_view response_body);

MetadataRecord parse_xmcd_record(
    std::string_view response_body,
    const std::string& category,
    const std::string& record_disc_id,
    const std::string& computed_disc_id,
    const std::string& provider_name);

class CddbCompatibleProvider final : public MetadataProvider {
public:
    explicit CddbCompatibleProvider(
        CddbClientConfig config = {},
        std::shared_ptr<CddbHttpTransport> transport = nullptr);

    std::string providerName() const override;

    const CddbClientConfig& config() const noexcept { return config_; }

    std::vector<MetadataCandidate> search(
        const DiscInfo& disc_info,
        const std::vector<TrackInfo>& tracks) override;

    MetadataRecord fetch(
        const MetadataCandidate& candidate,
        const DiscInfo& disc_info,
        const std::vector<TrackInfo>& tracks) override;

private:
    CddbClientConfig config_;
    std::shared_ptr<CddbHttpTransport> transport_;
};

} // namespace mycds
