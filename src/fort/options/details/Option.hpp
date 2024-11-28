#pragma once

#include "Traits.hpp"
#include "magic_enum/magic_enum.hpp"
#include <cctype>
#include <ios>
#include <iterator>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <fort/utils/Defer.hpp>

namespace fort {
namespace options {
namespace details {

struct OptionData {
	char        ShortFlag;
	std::string Name, Description;
	size_t      NumArgs = 1;
	bool        Required;
	bool        Repeatable;
};

class ParseError : public std::runtime_error {
public:
	ParseError(const std::string &name, const std::string &value) noexcept
	    : std::runtime_error{"could not parse " + name + "='" + value + "'"} {}

	ParseError(
	    const std::string &name,
	    const std::string &value,
	    const std::string &reason
	) noexcept
	    : std::runtime_error{
	          "could not parse " + name + "='" + value + "': " + reason} {}
};

#ifdef CHARIS_OPTIONS_USE_MAGIC_ENUM
template <typename T, std::enable_if_t<std::is_enum_v<T>> * = nullptr>
class ParseEnumError : public ParseError {
public:
	ParseEnumError(const std::string &name, const std::string &value)
	    : ParseError{name, value, reason()} {}

private:
	static std::string reason() {
		constexpr auto    &entries = magic_enum::enum_values<T>();
		std::ostringstream oss;
		std::string        prefix{"['"};

		oss << "possible enum values are ";

		for (const auto &e : entries) {
			oss << prefix << magic_enum::enum_name(e);
			prefix = "', '";
		}

		oss << "']";
		return oss.str();
	}
};
#endif

class OptionBase : private OptionData {
public:
	OptionBase(OptionData &&args)
	    : OptionData{std::move(args)} {}

	virtual ~OptionBase() = default;

	virtual void Parse(const std::optional<std::string> &) = 0;
	virtual void Format(std::ostream &out) const           = 0;

	inline const std::string &Name() const noexcept {
		return OptionData::Name;
	}

	inline const std::string &Description() const noexcept {
		return OptionData::Description;
	}

	inline char Short() const noexcept {
		return OptionData::ShortFlag;
	}

	bool Required() const noexcept {
		return OptionData::Required;
	}

	bool Repeatable() const noexcept {
		return OptionData::Repeatable;
	}

	size_t NumArgs() const noexcept {
		return OptionData::NumArgs;
	}

	void SetRequired(bool required) noexcept {
		this->OptionData::Required = required;
	}

protected:
	template <typename T>
	inline void Parse(const std::string &value, T &res) const {
		if constexpr (std::is_enum_v<T>) {
#ifdef CHARIS_OPTIONS_USE_MAGIC_ENUM
			auto v = magic_enum::enum_cast<T>(value);
			if (v.has_value() == false) {
				throw ParseEnumError<T>{OptionData::Name, value};
			}
			res = v.value();
#else
			throw std::logic_error(
			    "Enum are not supported, please link with magic_enum"
			);
#endif

		} else {
			std::istringstream iss(value);
			iss >> res;
			if (iss.fail()) {
				throw ParseError{OptionData::Name, value};
			}
		}
	}

	template <typename T>
	inline static void Format(std::ostream &out, const T &value) {
		if constexpr (std::is_enum_v<T>) {
#ifdef CHARIS_OPTIONS_USE_MAGIC_ENUM
			out << magic_enum::enum_name(value);
#else
			throw std::logic_error(
			    "Enum are not supported, please link with magic_enum"
			);
#endif
		} else {
			auto f = out.flags();
			defer {
				out.flags(f);
			};
			out << std::boolalpha << value;
		}
	}
};

template <>
inline void OptionBase::Parse<std::string>(
    const std::string &value, std::string &res
) const {
	res = value;
}

template <>
inline void OptionBase::Parse<bool>(const std::string &value, bool &res) const {
	if (value.empty() || value == "true") {
		res = true;
	} else if (value == "false") {
		res = false;
	} else {
		throw ParseError{Name(), value};
	}
}

struct OptionArgs {
	char        ShortFlag;
	std::string Name;
	std::string Description;
	bool        Required;
};

template <typename T, std::enable_if_t<is_optionable_v<T>> * = nullptr>
class Option : public OptionBase {
public:
	Option(OptionArgs &&args)
	    : OptionBase{{
	          .ShortFlag   = args.ShortFlag,
	          .Name        = args.Name,
	          .Description = args.Description,
	          .NumArgs     = std::is_same_v<T, bool> ? 0 : 1,
	          .Required    = std::is_same_v<T, bool> ? false : true,
	          .Repeatable  = false,
	      }} {
		if constexpr (std::is_fundamental_v<T>) {
			value = 0;
		}

#ifdef CHARIS_OPTIONS_USE_MAGIC_ENUM
		if constexpr (std::is_enum_v<T>) {
			if constexpr (magic_enum::enum_count<T>() > 0) {
				value = magic_enum::enum_values<T>()[0];
			}
		}
#endif
	}

	void Parse(const std::optional<std::string> &value) override {
		if (NumArgs() > 0 && value.has_value() == false) {
			throw ParseError{Name(), ""};
		}
		OptionBase::Parse<T>(value.value_or(""), this->value);
	}

	void Format(std::ostream &out) const override {
		OptionBase::Format<T>(out, value);
	}

	void SetDefault(const T &value) {
		SetRequired(false);
		this->value = value;
	}

	T value;
};

template <typename T, std::enable_if_t<is_optionable_v<T>> * = nullptr>
class RepeatableOption : public OptionBase {
public:
	RepeatableOption(OptionArgs &&args)
	    : OptionBase{{
	          .ShortFlag   = args.ShortFlag,
	          .Name        = args.Name,
	          .Description = args.Description,
	          .NumArgs     = std::is_same_v<T, bool> ? 0 : 1,
	          .Required    = false,
	          .Repeatable  = false,
	      }} {}

	void Parse(const std::optional<std::string> &value) override {
		if (NumArgs() > 0 && value.has_value() == false) {
			throw ParseError{Name(), ""};
		}

		OptionBase::Parse<T>(value.value_or(""), this->value.emplace_back());
	}

	void Format(std::ostream &out) const override {
		std::string sep = "";
		out << std::boolalpha << "[";
		for (const auto &v : value) {
			out << sep;
			OptionBase::Format<T>(out, v);
			sep = ", ";
		}
		out << "]";
	}

	void SetDefault(const std::vector<T> &value) {
		SetRequired(false);
		this->value = value;
	}

	std::vector<T> value;
};

} // namespace details
} // namespace options
} // namespace fort
