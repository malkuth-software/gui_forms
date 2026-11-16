#include "theme_media_query.hpp"

#include "../source_text/theme_source_text.hpp"

#include <algorithm>
#include <optional>
#include <stxlib/optional.hpp>
#include <utility>

namespace gui_forms::theme_language {

namespace {

enum class MediaFeature : std::uint8_t {
    colour_scheme,
    forced_colours,
    reduced_motion,
    resolution,
    minimum_resolution,
    maximum_resolution,
};

std::optional<MediaFeature> media_feature(std::string_view name) noexcept {
    if (equals_ascii_folded(name, "prefers-color-scheme")) return MediaFeature::colour_scheme;
    if (equals_ascii_folded(name, "forced-colors")) return MediaFeature::forced_colours;
    if (equals_ascii_folded(name, "prefers-reduced-motion")) return MediaFeature::reduced_motion;
    if (equals_ascii_folded(name, "resolution")) return MediaFeature::resolution;
    if (equals_ascii_folded(name, "min-resolution")) return MediaFeature::minimum_resolution;
    if (equals_ascii_folded(name, "max-resolution")) return MediaFeature::maximum_resolution;
    return std::nullopt;
}

bool resolution_feature(MediaFeature feature) noexcept {
    const bool resolution = feature == MediaFeature::resolution || feature == MediaFeature::minimum_resolution ||
                            feature == MediaFeature::maximum_resolution;
    return resolution;
}

// The keyword pair of a keyword feature: the one meaning true first.
struct FeatureKeywords final {
    std::string_view yes{};
    std::string_view no{};
};

FeatureKeywords feature_keywords(MediaFeature feature) noexcept {
    switch (feature) {
    case MediaFeature::colour_scheme: return FeatureKeywords{"dark", "light"};
    case MediaFeature::forced_colours: return FeatureKeywords{"active", "none"};
    case MediaFeature::reduced_motion: return FeatureKeywords{"reduce", "no-preference"};
    case MediaFeature::resolution:
    case MediaFeature::minimum_resolution:
    case MediaFeature::maximum_resolution:
    default: return FeatureKeywords{};
    }
}

bool trivia(TokenKind kind) noexcept {
    const bool skipped = kind == TokenKind::whitespace || kind == TokenKind::comment;
    return skipped;
}

// A keyword feature's value joined into a query: a second, different
// value makes the query one no window meets.
void join_keyword(stxlib::Optional<bool>& held, bool value, MediaQuery& query) noexcept {
    const bool* const before = held.value();
    if (before != nullptr && *before != value) query.never = true;
    held.set(value);
}

void join_minimum(stxlib::Optional<double>& held, double value) noexcept {
    const double* const before = held.value();
    const double joined = before != nullptr ? std::max(*before, value) : value;
    held.set(joined);
}

void join_maximum(stxlib::Optional<double>& held, double value) noexcept {
    const double* const before = held.value();
    const double joined = before != nullptr ? std::min(*before, value) : value;
    held.set(joined);
}

class QueryReader final {
public:
    QueryReader(std::string_view text, const ThemeTokenBuffer& tokens, TokenRange prelude,
                MediaQueryProblem& problem) noexcept
        : text_(text), tokens_(tokens), problem_(problem), next_(prelude.first.value), end_(prelude.end.value) {}

    MediaQueryOutcome read(std::vector<MediaQuery>& queries) {
        queries.clear();
        skip_trivia();
        if (next_ == end_) {
            queries.push_back(MediaQuery{});
            return MediaQueryOutcome::read;
        }
        while (true) {
            MediaQuery query{};
            if (!read_query(query)) return MediaQueryOutcome::refused;
            queries.push_back(query);
            if (next_ == end_) return MediaQueryOutcome::read;
            // read_query stops only at the end or at a comma.
            const std::uint32_t comma = next_;
            ++next_;
            skip_trivia();
            if (next_ == end_) {
                fail(DiagnosticCode::value_syntax, comma, "a media query after ','");
                return MediaQueryOutcome::refused;
            }
        }
    }

private:
    void skip_trivia() noexcept {
        while (next_ < end_ && trivia(tokens_.tokens[next_].kind)) ++next_;
    }

    [[nodiscard]] TokenKind kind_at(std::uint32_t index) const noexcept { return tokens_.tokens[index].kind; }

    [[nodiscard]] std::string_view text_at(std::uint32_t index) const noexcept {
        const Token& token = tokens_.tokens[index];
        const std::string_view text = text_.substr(token.offset.value, token.length.value);
        return text;
    }

    [[nodiscard]] bool ident_is(std::uint32_t index, std::string_view lower_name) const noexcept {
        const bool named = kind_at(index) == TokenKind::ident && equals_ascii_folded(text_at(index), lower_name);
        return named;
    }

    [[nodiscard]] bool query_ends() const noexcept {
        const bool ends = next_ == end_ || kind_at(next_) == TokenKind::comma;
        return ends;
    }

    bool fail(DiagnosticCode code, std::uint32_t token, std::string detail) {
        problem_.code = code;
        problem_.token = token;
        problem_.detail = std::move(detail);
        return false;
    }

    bool fail_with_text(DiagnosticCode code, std::uint32_t token) {
        return fail(code, token, clipped_theme_detail(text_at(token)));
    }

    // A keyword CSS writes in a query that the language refuses.
    [[nodiscard]] bool refused_keyword(std::uint32_t index) const noexcept {
        const bool refused = ident_is(index, "not") || ident_is(index, "only") || ident_is(index, "or");
        return refused;
    }

    bool read_query(MediaQuery& query) {
        const std::uint32_t first = next_;
        if (kind_at(first) == TokenKind::ident) {
            if (refused_keyword(first)) return fail_with_text(DiagnosticCode::value_form_not_admitted, first);
            if (ident_is(first, "and")) return fail(DiagnosticCode::value_syntax, first, "a media type or feature");
            if (!ident_is(first, "all") && !ident_is(first, "screen")) {
                return fail_with_text(DiagnosticCode::value_form_not_admitted, first);
            }
            ++next_;
            skip_trivia();
            if (query_ends()) return true;
            if (refused_keyword(next_)) return fail_with_text(DiagnosticCode::value_form_not_admitted, next_);
            if (!ident_is(next_, "and")) return fail(DiagnosticCode::value_syntax, next_, "'and'");
            ++next_;
            skip_trivia();
            if (next_ == end_) return fail(DiagnosticCode::value_syntax, first, "a media feature after 'and'");
        }
        while (true) {
            if (!read_feature(query)) return false;
            skip_trivia();
            if (query_ends()) return true;
            if (refused_keyword(next_)) return fail_with_text(DiagnosticCode::value_form_not_admitted, next_);
            if (!ident_is(next_, "and")) return fail(DiagnosticCode::value_syntax, next_, "'and'");
            const std::uint32_t joiner = next_;
            ++next_;
            skip_trivia();
            if (next_ == end_) return fail(DiagnosticCode::value_syntax, joiner, "a media feature after 'and'");
        }
    }

    // Past the ')' that closes the feature opened before `next_`, an
    // unknown feature's tokens unread.
    bool skip_feature(std::uint32_t opener) {
        std::uint32_t depth = 1U;
        while (next_ < end_) {
            const TokenKind kind = kind_at(next_);
            ++next_;
            if (kind == TokenKind::left_paren || kind == TokenKind::function) depth = depth + 1U;
            if (kind == TokenKind::right_paren) {
                depth = depth - 1U;
                if (depth == 0U) return true;
            }
        }
        return fail(DiagnosticCode::value_syntax, opener, "')'");
    }

    bool read_feature(MediaQuery& query) {
        const std::uint32_t opener = next_;
        if (kind_at(opener) == TokenKind::function) {
            return fail_with_text(DiagnosticCode::value_form_not_admitted, opener);
        }
        if (kind_at(opener) != TokenKind::left_paren) {
            return fail(DiagnosticCode::value_syntax, opener, "a media feature in parentheses");
        }
        ++next_;
        skip_trivia();
        if (next_ == end_) return fail(DiagnosticCode::value_syntax, opener, "a media feature");
        const std::uint32_t name = next_;
        if (kind_at(name) == TokenKind::left_paren || refused_keyword(name)) {
            return fail(DiagnosticCode::value_form_not_admitted, name, "a nested media condition");
        }
        if (kind_at(name) != TokenKind::ident) return fail_with_text(DiagnosticCode::value_syntax, name);
        const std::optional<MediaFeature> feature = media_feature(text_at(name));
        ++next_;
        if (!feature) {
            if (!query.unknown_feature) {
                query.unknown_feature = true;
                query.unknown_token = name;
            }
            return skip_feature(opener);
        }
        skip_trivia();
        if (next_ == end_) return fail(DiagnosticCode::value_syntax, opener, "')'");
        if (kind_at(next_) == TokenKind::right_paren) {
            return fail(DiagnosticCode::value_form_not_admitted, name, "a media feature without a value");
        }
        if (kind_at(next_) == TokenKind::delim) {
            return fail(DiagnosticCode::value_form_not_admitted, next_, "a media range");
        }
        if (kind_at(next_) != TokenKind::colon) return fail(DiagnosticCode::value_syntax, next_, "':'");
        ++next_;
        skip_trivia();
        if (next_ == end_) return fail(DiagnosticCode::value_syntax, opener, "a media feature's value");
        const std::uint32_t value = next_;
        if (kind_at(value) == TokenKind::function || kind_at(value) == TokenKind::left_paren) {
            return fail_with_text(DiagnosticCode::value_form_not_admitted, value);
        }
        ++next_;
        skip_trivia();
        if (next_ == end_ || kind_at(next_) != TokenKind::right_paren) {
            const std::uint32_t at = next_ == end_ ? value : next_;
            return fail(DiagnosticCode::value_syntax, at, "')'");
        }
        ++next_;
        if (resolution_feature(*feature)) return read_resolution(*feature, value, query);
        return read_keyword(*feature, value, query);
    }

    bool read_keyword(MediaFeature feature, std::uint32_t value, MediaQuery& query) {
        if (kind_at(value) != TokenKind::ident) return fail_with_text(DiagnosticCode::value_wrong_kind, value);
        const FeatureKeywords keywords = feature_keywords(feature);
        const bool yes = equals_ascii_folded(text_at(value), keywords.yes);
        const bool no = equals_ascii_folded(text_at(value), keywords.no);
        if (!yes && !no) return fail_with_text(DiagnosticCode::keyword_unknown, value);
        MediaCondition& condition = query.condition;
        switch (feature) {
        case MediaFeature::colour_scheme: join_keyword(condition.dark_scheme, yes, query); break;
        case MediaFeature::forced_colours: join_keyword(condition.forced_colours, yes, query); break;
        case MediaFeature::reduced_motion: join_keyword(condition.reduced_motion, yes, query); break;
        case MediaFeature::resolution:
        case MediaFeature::minimum_resolution:
        case MediaFeature::maximum_resolution:
        default: break;
        }
        return true;
    }

    bool read_resolution(MediaFeature feature, std::uint32_t value, MediaQuery& query) {
        if (kind_at(value) != TokenKind::dimension) {
            return fail(DiagnosticCode::value_wrong_kind, value, "a resolution in dppx or x");
        }
        const Token& token = tokens_.tokens[value];
        const ThemeNumber& number = tokens_.numbers[token.number];
        const std::string_view unit = text_at(value).substr(number.numeric_length.value);
        if (!equals_ascii_folded(unit, "dppx") && !equals_ascii_folded(unit, "x")) {
            return fail_with_text(DiagnosticCode::unit_not_admitted, value);
        }
        if (!(number.value > 0.0)) return fail_with_text(DiagnosticCode::value_out_of_range, value);
        MediaCondition& condition = query.condition;
        if (feature != MediaFeature::maximum_resolution) join_minimum(condition.minimum_resolution, number.value);
        if (feature != MediaFeature::minimum_resolution) join_maximum(condition.maximum_resolution, number.value);
        return true;
    }

    std::string_view text_;
    const ThemeTokenBuffer& tokens_;
    MediaQueryProblem& problem_;
    std::uint32_t next_{};
    std::uint32_t end_{};
};

} // namespace

MediaQueryOutcome read_media_queries(std::string_view text, const ThemeTokenBuffer& tokens, TokenRange prelude,
                                     std::vector<MediaQuery>& queries, MediaQueryProblem& problem) {
    QueryReader reader(text, tokens, prelude, problem);
    const MediaQueryOutcome outcome = reader.read(queries);
    return outcome;
}

} // namespace gui_forms::theme_language
