#include "Log.h"
#include <iostream>				   
#include <chrono>  
#include <filesystem>


namespace TLotSB {

	std::unique_ptr<Log> Log::instance_;

	namespace {

		/*vrací string podle formátu s placeholdery nahrazenými skuteènými èísly; pro milisekundy placeholder '%ms'*/
		std::string getTimeString(const std::string& format) {
			std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
			std::time_t cas = std::chrono::system_clock::to_time_t(now);
			tm time = *std::localtime(&cas);
			constexpr size_t BUFFER_SIZE = 0x100;
			char buffer[BUFFER_SIZE];
			std::memset(buffer, 0, BUFFER_SIZE);
			if (format.find("%ms") == std::string::npos) {	  //pokud si caller nevyžádá milisekundy, jenom formátujeme a vracíme
				std::strftime(buffer, BUFFER_SIZE, format.c_str(), &time);
				return buffer;
			}
			//pokud si vyžádá caller i milisekundy
			char miliBuffer[3];
			for (unsigned milisekundy = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000  //fill milisecond buffer
				, i = 0; i < 3; ++i) {
				miliBuffer[2 - i] = milisekundy % 10 + '0';
				milisekundy /= 10;
			}
			std::vector<std::pair<size_t, std::unique_ptr<char[]>>> slices;
			std::string_view view = format;
			for (size_t pozice; (pozice = view.find("%ms")) != std::string_view::npos;) {		  //rozsekání stringu 'format' na jednotlivé kusy podle delimiteru "%ms"
				slices.emplace_back(pozice, std::make_unique<char[]>(pozice + 1));
				static_cast<char*>(std::memcpy(slices.back().second.get(), view.data(), pozice))[pozice] = '\0';
				view.remove_prefix(pozice + 3);
			}
			char *ptr, *last = ptr = buffer;
			size_t cisloIterace = 0;
			for (const auto&[pocet, slice] : slices) {										   //slepí kusy bývalého stringu a dodá hodnotu milisekund
				++cisloIterace;
				if (pocet) {
					ptr += std::strftime(ptr, buffer + BUFFER_SIZE - ptr, slice.get(), &time);
					if (ptr == last)
						std::cout << "\aChyba pøi " << cisloIterace << ". volání std::strftime('" << slice.get() << "'); zapsáno nula bajtù";
					last = ptr;
				}
				std::memcpy(ptr, miliBuffer, 3);
				ptr += 3;
			}
			return buffer;
		}

		signed stoi(std::string_view view, signed base = 10) {
			if (base < 2)
				throw std::logic_error("Base menší než dva");
			auto getVal = [base](char c) -> signed {
				if (isdigit(static_cast<unsigned char>(c)))
					return c - '0' < base ? c - '0' : -1;
				if (!isalpha(static_cast<unsigned char>(c)))
					throw std::logic_error("Pøi parsování nalezen nealfanumerický znak");
				c -= islower(static_cast<unsigned char>(c)) ? 'a' : 'A';
				return c < base - 10 ? c : -1;
			};
			std::string_view::const_iterator begin = view.begin(), end = view.end();
			while (begin != end && isspace(static_cast<unsigned char>(*begin)))
				++begin;
			if (begin == end)
				return 0;
			bool jeMinus = *begin == '-';
			if (jeMinus || *begin == '+')
				++begin;
			signed result = 0;
			for (signed val; begin != end && (val = getVal(*begin), val != -1); ++begin)
				result = result * base + val;
			if (jeMinus)
				result *= -1;
			return result;
		}

		std::filesystem::path createLogFile(std::string_view header, std::string_view suffix) {
			size_t highestIndex = 0;
			for (std::filesystem::directory_entry entry : std::filesystem::directory_iterator(".")) {
				std::string str = entry.path().string();
				if (size_t pozice = str.find(header); pozice != std::string::npos) {
					pozice += header.size() + 1;   //pøiètení jednièky za underscore ve jménì
					size_t index = pozice;
					for (char c; c = str[index], c <= '9' && c >= '0'; ++index);
					index = stoi(std::string_view(str.c_str() + pozice, index - pozice));
					if (index > highestIndex)
						highestIndex = index;
				}
			}
			std::string cesta;
			cesta.reserve(512);
			cesta.append(header)
				.append("_")
				.append(std::to_string(highestIndex + 1))
				.append(getTimeString(".Log_dne_%Y_%m_%d_v_%H_%M_%S_%ms."))
				.append(suffix);
			std::filesystem::path path(cesta);
			if (std::filesystem::exists(path))
				std::cout << __FUNCTION__ << ": Vytvoøená cesta " << path << " již existuje.";
			return path;
		}
	}


	std::ostream& operator<<(std::ostream& stream, Log::Priority p) {
		using Priority = Log::Priority;
		switch (p) {
			case Priority::err:
				return stream << "Error: ";
			case Priority::log:
				return stream << "\tLog: ";
			case Priority::warning:
				return stream << "\tWarning: ";
			case Priority::prompt:
				return stream << "\tPrompt: ";
			case Priority::input:
				return stream << "\tInput: ";
			default:
				Log::error() << __FUNCTION__ << ": Not implemented token " << int(p);
		}
	}

	Log::Log(std::string_view header, std::string_view suffix)
		: priority_{ Priority::err, Priority::warning, Priority::log, Priority::prompt, Priority::input },
		running_(true) {
		std::lock_guard<std::mutex> guard(generalMutex_);
		std::locale::global(std::locale("Czech"));
		if (isInitialized())
			error() << __FUNCTION__ << ": Log již zøejmì byl konstruován";
		fileStream_.open(createLogFile(header, suffix));
		for (Priority priority : priority_) {
			fronty_.emplace(priority, std::queue<std::pair<std::string, std::string>>());
			mutexPtrs_.emplace(priority, std::make_unique<std::mutex>());
		}
		vlakno_ = std::thread(&Log::vypisovaciLoop, this);
	}

	Log::~Log() {
		running_ = false;
		flush();
		vlakno_.join();
	}

	void Log::vypisovaciLoop() {
		auto predicate = [&]() -> bool {
			if (!running_)
				return true;
			for (const auto&[priorita, fronta] : fronty_)
				if (fronta.size())
					return true;
			return false;
		};
		std::unique_lock<std::mutex> lock(generalMutex_);	 //prázdný lock jen pro cond_var; zamykání mutexù probíhá až v tìle cyklu
		while (running_) {
			condVar_.wait(lock, predicate);
			for (Priority priority : priority_) {
				std::unique_lock<std::mutex> queue_lock(*mutexPtrs_[priority], std::try_to_lock);
				if (!queue_lock.owns_lock())			   //pokus o zamèení mutexu; pokud selže, pøeskoèí iteraci
					continue;							   //nepodaøí-li se získat mutex, neblokujeme a pokraèujeme na další prioritu
				std::queue<std::pair<std::string, std::string>>& fronta = fronty_[priority];
				for (size_t size = fronta.size(); size; --size) {
					const auto&[time, msg] = fronta.front();
					switch (priority) {
						//switch how to write to std::cout
						case Priority::err:
						case Priority::warning:
						case Priority::log:
							std::cout << priority << time << '\n' << msg << std::endl;
							break;
						case Priority::prompt:
							std::cout << msg << std::endl;
						case Priority::input:
							break;
						default:
							Log::error() << __FUNCTION__ << ": Not implemented token " << priority << '(' << unsigned(priority) << ')';
					}
					fileStream_ << priority << time << '\n' << msg << std::endl;
					fronta.pop();
				}
			}
		}
	}

	void Log::insertString(const std::string& str, Log::Priority p) {
		insertString(std::string(str), p);
	}

	void Log::insertString(std::string&& str, Log::Priority p) {
		std::pair<std::string, std::string> pair = std::make_pair(getTimeString("%H:%M:%S.%ms"), std::forward<std::string>(str));
		{
			std::lock_guard<std::mutex> guard(*mutexPtrs_[p]);
			fronty_[p].push(std::move(pair));
		}
		flush();
	}

	bool Log::isInitialized() {
		return instance_.get();
	}

	void Log::flush() {
		instance_->condVar_.notify_one();
	}

	void Log::initialize(std::string_view header, std::string_view suffix) {
		if (isInitialized())
			Log::error() << __FUNCTION__ << ": Inicializace již jednou zavolána!";
		else
			instance_ = std::make_unique<Log>(header, suffix);
	}

	Log::Formatter<Log::Priority::err> Log::error() {
		return instance_->createFormatter<Priority::err>();
	}

	void Log::error(const std::string& str) {
		instance_->insertString(str, Priority::err);
	}

	void Log::error(std::string&& str) {
		instance_->insertString(std::forward<std::string>(str), Priority::err);
	}

	Log::Formatter<Log::Priority::warning> Log::warning() {
		return instance_->createFormatter<Priority::warning>();
	}

	void Log::warning(const std::string& str) {
		instance_->insertString(str, Priority::warning);
	}

	void Log::warning(std::string&& str) {
		instance_->insertString(std::forward<std::string>(str), Priority::warning);
	}

	Log::Formatter<Log::Priority::log> Log::log() {
		return instance_->createFormatter<Priority::log>();
	}

	void Log::log(const std::string& str) {
		instance_->insertString(str, Priority::log);
	}
	void Log::log(std::string&& str) {
		instance_->insertString(std::forward<std::string>(str), Priority::log);
	}

	Log::Formatter<Log::Priority::prompt> Log::prompt() {
		return instance_->createFormatter<Priority::prompt>();
	}

	void Log::prompt(const std::string& str) {
		instance_->insertString(str, Priority::prompt);
	}

	void Log::prompt(std::string&& str) {
		instance_->insertString(std::forward<std::string>(str), Priority::prompt);
	}

	Log::Formatter<Log::Priority::input> Log::input() {
		return instance_->createFormatter<Priority::input>();
	}
}