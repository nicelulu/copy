#pragma once

#include <cerrno>
#include <vector>

#include <Poco/Exception.h>
#include <Poco/SharedPtr.h>

#include <DB/Common/StackTrace.h>

namespace Poco { class Logger; }


namespace DB
{

class Exception : public Poco::Exception
{
public:
	Exception(int code = 0) : Poco::Exception(code) {}
	Exception(const std::string & msg, int code = 0) : Poco::Exception(msg, code) {}
	Exception(const std::string & msg, const std::string & arg, int code = 0) : Poco::Exception(msg, arg, code) {}
	Exception(const std::string & msg, const Exception & exc, int code = 0) : Poco::Exception(msg, exc, code), trace(exc.trace) {}
	Exception(const Exception & exc) : Poco::Exception(exc), trace(exc.trace) {}
	explicit Exception(const Poco::Exception & exc) : Poco::Exception(exc.displayText()) {}
	~Exception() throw() override {}
	Exception & operator = (const Exception & exc)
	{
		Poco::Exception::operator=(exc);
		trace = exc.trace;
		return *this;
	}
	const char * name() const throw() override { return "DB::Exception"; }
	const char * className() const throw() override { return "DB::Exception"; }
	DB::Exception * clone() const override { return new DB::Exception(*this); }
	void rethrow() const override { throw *this; }

	/// Дописать к существующему сообщению что-нибудь ещё.
	void addMessage(const std::string & arg) { extendedMessage(arg); }

	const StackTrace & getStackTrace() const { return trace; }

private:
	StackTrace trace;
};


/// Содержит дополнительный член saved_errno. См. функцию throwFromErrno.
class ErrnoException : public Exception
{
public:
    ErrnoException(int code = 0, int saved_errno_ = 0)
		: Exception(code), saved_errno(saved_errno_) {}
	ErrnoException(const std::string & msg, int code = 0, int saved_errno_ = 0)
		: Exception(msg, code), saved_errno(saved_errno_) {}
	ErrnoException(const std::string & msg, const std::string & arg, int code = 0, int saved_errno_ = 0)
		: Exception(msg, arg, code), saved_errno(saved_errno_) {}
	ErrnoException(const std::string & msg, const Exception & exc, int code = 0, int saved_errno_ = 0)
		: Exception(msg, exc, code), saved_errno(saved_errno_) {}
	ErrnoException(const ErrnoException & exc)
		: Exception(exc), saved_errno(exc.saved_errno) {}

	int getErrno() const { return saved_errno; }

private:
	int saved_errno;
};


typedef Poco::SharedPtr<Poco::Exception> ExceptionPtr;
typedef std::vector<ExceptionPtr> Exceptions;


void throwFromErrno(const std::string & s, int code = 0, int the_errno = errno);


/** Для использования в блоке catch (...).
  * Преобразует Exception, Poco::Exception, std::exception или неизвестный exception в ExceptionPtr.
  */
ExceptionPtr cloneCurrentException();

/** Попробовать записать исключение в лог (и забыть про него).
  * Можно использовать в деструкторах в блоке catch (...).
  */
void tryLogCurrentException(const char * log_name, const std::string & start_of_message = "");
void tryLogCurrentException(Poco::Logger * logger, const std::string & start_of_message = "");

std::string getCurrentExceptionMessage(bool with_stacktrace);


void rethrowFirstException(Exceptions & exceptions);

}
