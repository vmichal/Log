#pragma once
#ifndef LOG_H
#define LOG_H

#include <string>
#include <string_view>
#include <queue>
#include <iostream>
#include <sstream>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <map>
#include <fstream>
#include <locale>
#include <memory>

namespace TLotSB {

	class Log {
	public:
		struct formatEnd_t {};
		/*Okamžitì ukonèí formátování a odešle zprávu k vypsání*/
		static inline constexpr formatEnd_t formatEnd = {};
		enum class Priority { err, warning, log, prompt, input };

		template<class _Ty,
			class... _Types,
			std::enable_if_t<!std::is_array_v<_Ty>, int> = 0>
			friend _NODISCARD std::unique_ptr<_Ty> std::make_unique(_Types&&... _Args);

	private:
		template<Priority P> class Formatter;

		static std::unique_ptr<Log> instance_;

		std::vector<Priority> priority_;
		bool running_;
		std::map<Priority, std::queue<std::pair<std::string, std::string>>> fronty_;
		std::map<Priority, std::unique_ptr<std::mutex>> mutexPtrs_;
		std::mutex generalMutex_;
		std::condition_variable condVar_;
		std::ofstream fileStream_;
		std::thread vlakno_;
																									   
		void insertString(std::string&& str, Priority priority);
		void insertString(const std::string& str, Priority priority);

		template<Priority P>
		Formatter<P> createFormatter() {
			return Formatter<P>(*this);
		}

		void vypisovaciLoop();

		Log(std::string_view header, std::string_view suffix);
	public:
		static void initialize(std::string_view header, std::string_view suffix);
		static void flush();
		static bool isInitialized();

		~Log();
		Log(Log&&) = delete;
		Log(const Log&) = delete;
		Log& operator=(const Log&) = delete;
		Log& operator=(Log&&) = delete;

		static Formatter<Priority::log> log();
		static void log(const std::string& str);
		static void log(std::string&& str);
		static Formatter<Priority::err> error();
		static void error(const std::string& str);
		static void error(std::string&& str);
		static Formatter<Priority::warning> warning();
		static void warning(const std::string& str);
		static void warning(std::string&& str);
		static Formatter<Priority::prompt> prompt();
		static void prompt(const std::string& str);
		static void prompt(std::string&& str);
		static Formatter<Priority::input> input();

	private:
		template<Priority P>
		struct FormatterBase {
		protected:
			Log & myLog_;
			std::unique_ptr<std::mutex> mutexPtr_; //unique_ptr, aby byl FormatterBase move-constructible, mutex by byl immutable
			std::ostringstream stringStream_;

		public:
			FormatterBase(Log& log)
				: myLog_(log), mutexPtr_(std::make_unique<std::mutex>()) {
				stringStream_.imbue(std::locale("Czech"));
			}

			virtual ~FormatterBase() noexcept {
				this->operator<<(formatEnd);
			}

			FormatterBase(FormatterBase&& moveCtr) = default;
			FormatterBase(const FormatterBase& copyCtor) = delete;

			void operator<<(formatEnd_t) {
				std::lock_guard<std::mutex> guard(*mutexPtr_);
				std::string str(stringStream_.str());
				if (str.size())
					myLog_.insertString(std::move(str), P);
				stringStream_.str("");
			}
		};

		template<Priority P>
		struct Formatter : public FormatterBase<P> {
			using MyBase = FormatterBase<P>;
			using MyBase::operator<<;

			Formatter(Log& log) : MyBase(log) {}

			Formatter(const Formatter<P>&) = delete;
			Formatter(Formatter<P>&&) = default;
			Formatter& operator=(const Formatter<P>&) = delete;
			Formatter& operator=(Formatter<P>&&) = default;

			template<typename T>
			Formatter& operator<<(const T& t) {
				std::lock_guard<std::mutex> guard(*this->mutexPtr_);
				this->stringStream_ << t;
				return *this;
			}

			template<typename T>
			Formatter& operator<<(T&& t) {
				std::lock_guard<std::mutex> guard(*this->mutexPtr_);
				this->stringStream_ << t;
				return *this;
			}

			Formatter& operator<<(std::ostream& (*ptr)(std::ostream&)) {
				std::lock_guard<std::mutex> guard(*this->mutexPtr_);
				ptr(this->stringStream_);
				return *this;
			}
		};

		template<>
		struct Formatter<Priority::input> : public FormatterBase<Priority::input> {
			using MyBase = FormatterBase<Priority::input>;
			using MyBase::operator<<;

			Formatter(Log& log) : MyBase(log) {}

			Formatter(const Formatter<Priority::input>&) = delete;
			Formatter(Formatter<Priority::input>&&) = default;
			Formatter& operator=(const Formatter<Priority::input>&) = delete;
			Formatter& operator=(Formatter<Priority::input>&&) = default;

			template<typename T>
			Formatter& operator>>(T& t) {
				std::lock_guard<std::mutex> guard(*mutexPtr_);
				std::cin >> t;
				stringStream_ << t;
				return *this;
			}
		};
	};

	std::ostream& operator<<(std::ostream&, Log::Priority);
}

#endif