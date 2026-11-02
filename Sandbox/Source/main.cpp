




// ==========================================================================================================
// Fixed String
// ==========================================================================================================

//template <std::size_t N>
//struct FixedString
//{
//	// Constructor
//	inline consteval FixedString(const char(&string)[N])
//	{
//		for (int i = 0; i < N; ++i)
//		{
//			Value[i] = string[i];
//		}
//	}
//
//	// Member Functions
//
//	inline constexpr std::size_t size() const
//	{
//		return N - 1;
//	}
//
//	inline constexpr std::string_view View() const
//	{
//		return { Value, N - 1 };
//	}
//
//	// Member Variables
//	char Value[N];
//};
//
//
//
//
//// ==========================================================================================================
//// Format Operation Data
//// ==========================================================================================================
//
//struct FormatOpData
//{
//	FormatOpType Type;
//	std::size_t Offset = 0;
//	std::size_t Length = 0;
//	std::size_t ArgumentIndex = 0;
//};
//
//
//
//
//// ==========================================================================================================
//// Log Format Metadata
//// ==========================================================================================================
//
//template<std::size_t Capacity>
//struct LogFormatMetadata
//{
//	std::array<FormatOpData, Capacity> Operations{};
//
//	std::size_t OperationCount = 0;
//	std::size_t ArgumentCount = 0;
//};
//
//
//
//
//// ==========================================================================================================
//// Log Format Metadata Holder
//// ==========================================================================================================
//
//template<FixedString Format>
//struct LogFormatMetadataHolder
//{
//	static constexpr auto FormatString = Format;
//	static constexpr auto Metadata = ParseFormat<Format>();
//};
//
//
//
//
//
//
//
//
//
//
//// ==========================================================================================================
//// Format Functions
//// ==========================================================================================================
//
//template <FixedString F>
//consteval auto ParseFormat()
//{
//	LogFormatMetadata<F.size() + 1> metadata{};
//
//	std::size_t literalStart = 0;
//	std::size_t i = 0;
//
//	while (i < F.size())
//	{
//		// Ordinary Character, keep scanning the literal
//		if (F.Value[i] != '%')
//		{
//			++i;
//			continue;
//		}
//
//		if (i + 1 >= F.size())
//		{
//			throw "Incomplete Format specifier";
//		}
//
//		const char specifier = F.Value[i + 1];
//
//		// Set the literal string operation
//		if (i > literalStart)
//		{
//			metadata.Operations[metadata.OperationCount++] =
//			{
//				FormatOpType::Literal,
//				literalStart,
//				i - literalStart,
//				0
//			};
//		}
//
//		// Set the type specifier
//		FormatOpType type;
//
//		switch (F.Value[i + 1])
//		{
//		case 'd':
//		{
//			type = FormatOpType::Integer;
//			break;
//		}
//
//		case 'f':
//		{
//			type = FormatOpType::Float;
//			break;
//		}
//
//		case 'l':
//		{
//			type = FormatOpType::Double;
//			break;
//		}
//
//		case 'b':
//		{
//			type = FormatOpType::Bool;
//			break;
//		}
//
//		default:
//		{
//			throw "Unsupported format specifier";
//		}
//		}
//
//		// Set the type specifier operation
//		metadata.Operations[metadata.OperationCount++] =
//		{
//			type,
//			0,
//			0,
//			metadata.ArgumentCount++
//		};
//
//		// Skip % and its specifier
//		i += 2;
//
//		// The next literal begins after the placeholder
//		literalStart = i;
//	}
//
//	// Record any remaining literal text
//	if (literalStart < F.size())
//	{
//		metadata.Operations[metadata.OperationCount++] =
//		{
//			FormatOpType::Literal,
//			literalStart,
//			F.size() - literalStart,
//			0
//		};
//	}
//
//	return metadata;
//}
//
//
//
//// ==========================================================================================================
//// Log Arena
//// ==========================================================================================================
//
//class LogArena
//{
//public:
//
//	static constexpr std::size_t Capacity = 4096;
//
//	inline void Append(std::string_view text)
//	{
//
//		if (text.size() > Capacity - Used)
//		{
//			throw std::length_error("Log Arena Is Full");
//		}
//
//		for (char c : text)
//		{
//			Buffer[Used++] = c;
//		}
//	}
//
//	template<typename T>
//	void AppendNumber(T Value)
//	{
//		char temporary[64];
//
//		auto result = std::to_chars(temporary, temporary + sizeof(temporary), Value);
//
//		if (result.ec != std::errc{})
//		{
//			throw std::runtime_error("Numeric formatting failed");
//		}
//
//		Append({ temporary, static_cast<std::size_t>(result.ptr - temporary) });
//	}
//
//	// Returns the capacity of the buffer
//	inline std::size_t  GetCapacity() const
//	{
//		return Capacity - Used;
//	}
//
//
//	inline void Flush() const
//	{
//		std::fwrite(Buffer.data(), 1, Used, stdout);
//		std::fputc('\n', stdout);
//	}
//
//private:
//
//	std::array<char, Capacity> Buffer{};
//	std::size_t Used = 0;
//};
//
//
//
//
//
//
//
//
//
//
//template<std::size_t OpIndex,
//	FixedString Format,
//	typename Tuple>
//void ExecuteOperations(LogArena& Arena, Tuple& Arguments)
//{
//	constexpr auto metadata = ParseFormat<Format>();
//
//	if constexpr (OpIndex < metadata.OperationCount)
//	{
//		constexpr auto op = metadata.Operations[OpIndex];
//
//		if constexpr (op.Type == FormatOpType::Literal)
//		{
//			Arena.Append(Format.View().substr(op.Offset, op.Length));
//		}
//		else if constexpr (op.Type == FormatOpType::Integer)		// Integral Type
//		{
//			using T = std::remove_cvref_t < decltype(std::get<op.ArgumentIndex>(Arguments)) >;
//
//			static_assert(std::integral<T> && !std::same_as<T, bool>,
//				"%d requires an integral argument other than bool");
//
//			Arena.AppendNumber(std::get<op.ArgumentIndex>(Arguments));
//		}
//		else if constexpr (op.Type == FormatOpType::Float)		// Float Type
//		{
//			using T = std::remove_cvref_t < decltype(std::get<op.ArgumentIndex>(Arguments)) >;
//
//			static_assert(std::same_as<T, float>,
//				"%f requires an integral argument other than bool");
//
//			Arena.AppendNumber(std::get<op.ArgumentIndex>(Arguments));
//		}
//		else if constexpr (op.Type == FormatOpType::Double)		// Double Type
//		{
//			using T = std::remove_cvref_t < decltype(std::get<op.ArgumentIndex>(Arguments)) >;
//
//			static_assert(std::same_as<T, double>,
//				"%l requires an integral argument other than bool");
//
//			Arena.AppendNumber(std::get<op.ArgumentIndex>(Arguments));
//		}
//		else if constexpr (op.Type == FormatOpType::Bool)		// Double Type
//		{
//			using T = std::remove_cvref_t < decltype(std::get<op.ArgumentIndex>(Arguments)) >;
//
//			static_assert(std::same_as<T, bool>,
//				"%b requires an integral argument other than bool");
//
//			if (std::get<op.ArgumentIndex>(Arguments))
//			{
//				Arena.Append("true");
//			}
//			else
//			{
//				Arena.Append("false");
//			}
//		}
//
//		ExecuteOperations<OpIndex + 1, Format>(Arena, Arguments);
//	}
//}
//
//
//
//
//template<FixedString Format>
//consteval std::size_t CountFormatArguments()
//{
//	std::size_t count = 0;
//
//	for (std::size_t i = 0; i < Format.size(); ++i)
//	{
//		if (Format.Value[i] == '%')
//		{
//			if (i + 1 >= Format.size())
//			{
//				throw "Incomplete format specifier";
//			}
//
//			++count;
//			++i;
//		}
//	}
//
//	return count;
//}


int main()
{
	return 0;
}