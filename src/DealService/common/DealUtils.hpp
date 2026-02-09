#ifndef DEAL_UTILS_HPP
#define DEAL_UTILS_HPP

#include <cmath>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

class DealUtils
{
public:
    static int decimalsFromStep(double step)
    {
        if (!(step > 0.0) || std::isnan(step) || std::isinf(step))
            return 8;

        int decimals = 0;
        double scaled = step;

        while (scaled < 1.0 && decimals < 18)
        {
            scaled *= 10.0;
            ++decimals;
            if (std::fabs(scaled - std::round(scaled)) < 1e-12)
                break;
        }
        return decimals;
    }

    static std::string formatDecimal(double value, int decimals)
    {
        if (std::isnan(value) || std::isinf(value))
            throw std::runtime_error("formatDecimal: invalid floating value");

        if (decimals < 0) decimals = 0;
        if (decimals > 18) decimals = 18;

        std::ostringstream out;
        out.setf(std::ios::fixed);
        out << std::setprecision(decimals) << value;

        std::string text = out.str();
        while (text.size() > 1 && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
        if (text == "-0") text = "0";
        return text;
    }

    static std::string formatByStep(double value, double stepSize)
    {
        return formatDecimal(value, decimalsFromStep(stepSize));
    }

    static void verifyNoScientificNotation(std::string_view text)
    {
        if (containsExponentNumber(text))
            throw std::runtime_error(std::string("Scientific notation detected in outbound request: ") + std::string(text));
    }

private:
    static bool isBoundary(char ch)
    {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::isspace(c)) return true;
        if (ch == '&' || ch == '=' || ch == '?' || ch == ',' || ch == ':' || ch == '"' ||
            ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == '(' || ch == ')' ||
            ch == '<' || ch == '>' )
            return true;
        return false;
    }

    static bool isNumericStart(std::string_view text, size_t pos)
    {
        char ch = text[pos];

        if (pos > 0 && !isBoundary(text[pos - 1]))
            return false;

        if (std::isdigit(static_cast<unsigned char>(ch)))
            return true;

        if (ch == '+' || ch == '-')
        {
            if (pos + 1 >= text.size()) return false;
            char next = text[pos + 1];
            return (std::isdigit(static_cast<unsigned char>(next)) || next == '.');
        }

        if (ch == '.')
        {
            if (pos + 1 >= text.size()) return false;
            return std::isdigit(static_cast<unsigned char>(text[pos + 1]));
        }

        return false;
    }

    static bool containsExponentNumber(std::string_view text)
    {
        const size_t n = text.size();

        for (size_t start = 0; start < n; ++start)
        {
            if (!isNumericStart(text, start))
                continue;

            bool sawDigit = false;
            bool sawDot = false;

            size_t i = start;

            if (text[i] == '+' || text[i] == '-') ++i;

            for (; i < n; ++i)
            {
                char ch = text[i];

                if (std::isdigit(static_cast<unsigned char>(ch)))
                {
                    sawDigit = true;
                    continue;
                }

                if (ch == '.' && !sawDot)
                {
                    sawDot = true;
                    continue;
                }

                if ((ch == 'e' || ch == 'E') && sawDigit)
                {
                    if (i + 1 >= n) break;

                    size_t expPos = i + 1;
                    if (text[expPos] == '+' || text[expPos] == '-')
                    {
                        ++expPos;
                        if (expPos >= n) break;
                    }

                    if (std::isdigit(static_cast<unsigned char>(text[expPos])))
                        return true;

                    break;
                }

                break;
            }

            start = i;
        }

        return false;
    }
};

#endif