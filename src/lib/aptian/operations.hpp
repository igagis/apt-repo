/*
aptian - apt repository tool

Copyright (C) 2024  Ivan Gagis <igagis@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.*/

/* ================ LICENSE END ================ */

#pragma once

#include <string_view>

#include <utki/span.hpp>

namespace aptian {

void init( //
	std::string_view dir,
	std::string_view gpg
);

void add( //
	std::string_view dir,
	std::string_view dist,
	std::string_view comp,
	utki::span<const std::string> package_paths
);

} // namespace aptian
