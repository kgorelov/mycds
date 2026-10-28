#pragma once

#include "domain/disc_info.hpp"
#include "source/disc_source.hpp"

#include <string>

namespace mycds {

using DiscInspection = DiscLayout;

bool inspect_physical_cd(
    const std::string& device_path,
    DiscLayout& out_disc,
    std::string& out_error);

bool inspect_image(
    const std::string& image_path,
    DiscLayout& out_disc,
    std::string& out_error);

void print_disc_inspection(const DiscLayout& disc);

bool compare_disc_inspections(
    const DiscLayout& lhs,
    const DiscLayout& rhs);

} // namespace mycds
