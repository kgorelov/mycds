#pragma once

#include "domain/disc_info.hpp"
#include "domain/disc_metadata.hpp"
#include "metadata/metadata_matcher.hpp"
#include "metadata/metadata_provider.hpp"
#include "metadata/metadata_store.hpp"

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>

namespace mycds {

MetadataRecord extract_metadata_record(const DiscLayout& layout);

void print_metadata_summary(
    const DiscLayout& layout,
    std::ostream& out,
    const std::optional<MetadataValidationResult>& validation = std::nullopt);

bool run_interactive_metadata_lookup(
    DiscLayout& layout,
    MetadataProvider& provider,
    std::istream& in,
    std::ostream& out,
    const std::filesystem::path& sidecar_path = {},
    std::optional<std::size_t> preselected_one_based_index = std::nullopt,
    bool auto_save = false);

void run_interactive_metadata_edit(
    DiscLayout& layout,
    std::istream& in,
    std::ostream& out);

int run_metadata_repl(
    DiscLayout& layout,
    MetadataProvider& provider,
    std::istream& in,
    std::ostream& out,
    const std::filesystem::path& custom_sidecar_path = {});

} // namespace mycds
