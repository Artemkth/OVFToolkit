class ConsoleInfo
{
	struct ConsoleMetadata;
	const ConsoleMetadata* const meta{ nullptr };
public:
	//init internal data
	ConsoleInfo() noexcept;
	~ConsoleInfo() noexcept;

	int GetConsoleWidth() const;
	const bool isRedirected;
};
