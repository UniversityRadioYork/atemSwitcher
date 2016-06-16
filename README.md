# ATEM Switcher

[![Build Status](https://travis-ci.org/UniversityRadioYork/atemSwitcher.svg?branch=master)](https://travis-ci.org/UniversityRadioYork/atemSwitcher)

ATEM Switcher is a simple Arduino based controller for the Blackmagic ATEM Video Switchers. The software has two modes, manual where preview inputs can be selected and cut to live, and auto which uses the analogue inputs for reading audio signals from microphones.

## Installation

This project uses [platformIO](http://platformio.org/) follow their documentation to get your environment set up as you like, then run `platformio run` to build.

## Usage

The Arduino is designed to be used with buttons for 6 preview input selectors, cut, and mode change for switching between manual and auto. The inputs selectors each have an LED output, as does the mode.

## Contributing

1. Fork it!
2. Create your feature branch: `git checkout -b my-new-feature`
3. Commit your changes: `git commit -am 'Add some feature'`
4. Push to the branch: `git push origin my-new-feature`
5. Submit a pull request :D

## Credits

Designed and built by [Andy Durant](https://github.com/AJDurant) using the [SKAARHOJ ATEM Arduino Library](https://github.com/kasperskaarhoj/SKAARHOJ-Open-Engineering)

## License

This software is licensed under the ISC Licence.
The ATEM Libraries used are licensed under GNU-GPLv3.
