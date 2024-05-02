# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
### Changed
* Increased the font size.
* Significantly cleaned up the code:
  * Renamed several functions to more accurately describe what they do.
  * Used more Win32-like variable and type names like `HCLOCKWINDOW` instead of `struct clock_window*`.
  * Re-wrote `LayOutClockWindow()` to make it easier to understand.
### Fixed
* Only update the clock when the window is shown.
* Eliminated unnecessary re-drawing in `LayOutClockWindow()`.
* Added a missing check after a memory allocation in `CreateClockWindow()`.
* Cleaned up working but semantically-incorrect code, including:
  * Re-implementing keyboard shortcuts correctly using an accelerator table.
  * Setting the window background using `wc.hbrBackground` rather than by processing `WM_PAINT`.
  * Correcting several instances of `NULL` instead of `FALSE`.

## [1.0.2] - 2023-10-03
### Fixed
* Use the correct unit conversion for days in the uptime display.

## [1.0.1] - 2023-09-24
### Changed
* Close the window when Ctrl+W is pressed.
* Cleaned up the code.

## [1.0.0] - 2023-03-31
### Added
* Initial release.

[Unreleased]: https://github.com/bmjcode/uptime-clock/compare/v1.0.2...HEAD
[1.0.2]: https://github.com/bmjcode/uptime-clock/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/bmjcode/uptime-clock/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/bmjcode/uptime-clock/releases/tag/v1.0.0
