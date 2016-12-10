#pragma once

#include <common/Common.h>
#include <common/singleton.h>
#include <common/likely.h>
#include <common/strong_typedef.h>

#include <iostream>
#include <vector>
#include <unordered_map>
#include <ctime>

#define DATE_LUT_MIN 0
#define DATE_LUT_MAX (0x7FFFFFFF - 86400)
#define DATE_LUT_MAX_DAY_NUM (0x7FFFFFFF / 86400)
#define DATE_LUT_MIN_YEAR 1970
#define DATE_LUT_MAX_YEAR 2037	/// Последний полный год
#define DATE_LUT_YEARS 68		/// Количество лет в lookup таблице


STRONG_TYPEDEF(UInt16, DayNum_t);


/** Lookup table to conversion of time to date, and to month / year / day of week / day of month and so on.
  * First time was implemented for OLAPServer, that needed to do billions of such transformations.
  */
class DateLUTImpl
{
public:
	DateLUTImpl(const std::string & time_zone);

public:
	struct Values
	{
		/// 32 бита из time_t начала дня.
		/// Знаковость важна, чтобы поддержать начало 1970-01-01 MSK, которое имело time_t == -10800.
		/// Измените на time_t, если надо поддержать времена после 2038 года.
		Int32 date;

		UInt16 year;
		UInt8 month;
		UInt8 day_of_month;
		UInt8 day_of_week;
	};

private:
	std::string time_zone;

	/// Сравнительно много данных. То есть, лучше не класть объект на стек.
	/// По сравнению с std::vector, на один indirection меньше.
	Values lut[DATE_LUT_MAX_DAY_NUM + 1];

	/// lookup таблица начал годов
	DayNum_t years_lut[DATE_LUT_YEARS];

	/// Смещение от UTC в начале Unix эпохи.
	time_t offset_at_start_of_epoch;

	inline size_t findIndex(time_t t) const
	{
		/// первое приближение
		size_t precision = t / 86400;
		if (precision >= DATE_LUT_MAX_DAY_NUM)
			return 0;
		if (t >= lut[precision].date && t < lut[precision + 1].date)
			return precision;

		for (size_t i = 1;; ++i)
		{
			if (precision + i >= DATE_LUT_MAX_DAY_NUM)
				return 0;
			if (t >= lut[precision + i].date && t < lut[precision + i + 1].date)
				return precision + i;
			if (precision < i)
				return 0;
			if (t >= lut[precision - i].date && t < lut[precision - i + 1].date)
				return precision - i;
		}
	}

	inline const Values & find(time_t t) const
	{
		return lut[findIndex(t)];
	}

	static inline DayNum_t fixDay(DayNum_t day)
	{
		return day > DATE_LUT_MAX_DAY_NUM ? static_cast<DayNum_t>(0) : day;
	}

public:
	const std::string & getTimeZone() const { return time_zone; }

	/// всё ниже thread-safe; корректность входных данных не проверяется

	inline time_t 		toDate(time_t t) 		const {	return find(t).date; }
	inline unsigned 	toMonth(time_t t) 		const {	return find(t).month; }
	inline unsigned 	toYear(time_t t) 		const {	return find(t).year; }
	inline unsigned 	toDayOfWeek(time_t t) 	const {	return find(t).day_of_week; }
	inline unsigned 	toDayOfMonth(time_t t)	const {	return find(t).day_of_month; }

	/// номер недели, начиная с какой-то недели в прошлом; неделя начинается с понедельника
	/// (переводим к понедельнику и делим DayNum на 7; будем исходить из допущения,
	/// что в области применения этой функции не было и не будет недель, состоящих не из семи дней)
	inline unsigned		toRelativeWeekNum(DayNum_t d) const
	{
		return (d - (lut[d].day_of_week - 1)) / 7;
	}

	inline unsigned		toRelativeWeekNum(time_t t) const
	{
		size_t index = findIndex(t);
		return (index - (lut[index].day_of_week - 1)) / 7;
	}

	/// номер месяца, начиная с какого-то месяца в прошлом (год * 12 + номер месяца в году)
	inline unsigned		toRelativeMonthNum(DayNum_t d) const
	{
		return lut[d].year * 12 + lut[d].month;
	}

	inline unsigned		toRelativeMonthNum(time_t t) const
	{
		size_t index = findIndex(t);
		return lut[index].year * 12 + lut[index].month;
	}

	/// делим unix timestamp на 3600;
	/// (таким образом, учитываются прошедшие интервалы времени длительностью в час, не зависимо от перевода стрелок;
	/// поддерживаются только часовые пояса, в которых перевод стрелок осуществлялся только на целое число часов)
	inline time_t 		toRelativeHourNum(time_t t) const
	{
		return t / 3600;
	}

	/// делим unix timestamp на 60
	inline time_t 		toRelativeMinuteNum(time_t t) const
	{
		return t / 60;
	}

	/// округление вниз до понедельника
	inline time_t 		toFirstDayOfWeek(time_t t) const
	{
		size_t index = findIndex(t);
		return lut[index - (lut[index].day_of_week - 1)].date;
	}

	inline DayNum_t		toFirstDayNumOfWeek(DayNum_t d) const
	{
		return DayNum_t(d - (lut[d].day_of_week - 1));
	}

	inline DayNum_t		toFirstDayNumOfWeek(time_t t) const
	{
		size_t index = findIndex(t);
		return DayNum_t(index - (lut[index].day_of_week - 1));
	}

	/// округление вниз до первого числа месяца
	inline time_t		toFirstDayOfMonth(time_t t) const
	{
		size_t index = findIndex(t);
		return lut[index - (lut[index].day_of_month - 1)].date;
	}

	inline DayNum_t		toFirstDayNumOfMonth(DayNum_t d) const
	{
		return DayNum_t(d - (lut[fixDay(d)].day_of_month - 1));
	}

	inline DayNum_t		toFirstDayNumOfMonth(time_t t) const
	{
		size_t index = findIndex(t);
		return DayNum_t(index - (lut[index].day_of_month - 1));
	}

	/// округление до первого числа квартала
	inline time_t		toFirstDayOfQuarter(time_t t) const
	{
		size_t index = findIndex(t);
		switch (lut[index].month % 3)
		{
			case 0:
				index = index - lut[index].day_of_month;
			case 2:
				index = index - lut[index].day_of_month;
			case 1:
				index = index - lut[index].day_of_month + 1;
		}
		return DayNum_t(index);
	}

	inline DayNum_t		toFirstDayNumOfQuarter(DayNum_t d) const
	{
		size_t index = fixDay(d);
		switch (lut[index].month % 3)
		{
			case 0:
				index = index - lut[index].day_of_month;
			case 2:
				index = index - lut[index].day_of_month;
			case 1:
				index = index - lut[index].day_of_month + 1;
		}
		return DayNum_t(index);
	}

	inline DayNum_t		toFirstDayNumOfQuarter(time_t t) const
	{
		size_t index = findIndex(t);
		switch (lut[index].month % 3)
		{
			case 0:
				index = index - lut[index].day_of_month;
			case 2:
				index = index - lut[index].day_of_month;
			case 1:
				index = index - lut[index].day_of_month + 1;
		}
		return DayNum_t(index);
	}

	/// округление вниз до первого числа года
	inline time_t		toFirstDayOfYear(time_t t) const
	{
		return lut[years_lut[lut[findIndex(t)].year - DATE_LUT_MIN_YEAR]].date;
	}

	inline DayNum_t		toFirstDayNumOfYear(DayNum_t d) const
	{
		return years_lut[lut[fixDay(d)].year - DATE_LUT_MIN_YEAR];
	}

	inline time_t		toFirstDayNumOfYear(time_t t) const
	{
		return lut[years_lut[lut[findIndex(t)].year - DATE_LUT_MIN_YEAR]].date;
	}

	/// первое число следующего месяца
	inline time_t		toFirstDayOfNextMonth(time_t t) const
	{
		size_t index = findIndex(t);
		index += 32 - lut[index].day_of_month;
		return lut[index - (lut[index].day_of_month - 1)].date;
	}

	/// первое число предыдущего месяца
	inline time_t		toFirstDayOfPrevMonth(time_t t) const
	{
		size_t index = findIndex(t);
		index -= lut[index].day_of_month;
		return lut[index - (lut[index].day_of_month - 1)].date;
	}

	/// количество дней в месяце
	inline size_t		daysInMonth(time_t t) const
	{
		size_t today = findIndex(t);
		size_t start_of_month = today - (lut[today].day_of_month - 1);
		size_t next_month = start_of_month + 31;
		size_t start_of_next_month = next_month - (lut[next_month].day_of_month - 1);
		return start_of_next_month - start_of_month;
	}

	/** Округление до даты; затем сдвиг на указанное количество дней.
	  * Замечание: результат сдвига должен находиться в пределах LUT.
	  */
	inline time_t 		toDateAndShift(time_t t, int days = 1)	const
	{
		return lut[findIndex(t) + days].date;
	}

	/** функции ниже исходят из допущения, что перевод стрелок вперёд, если осуществляется, то на час, в два часа ночи,
	  *  а перевод стрелок назад, если осуществляется, то на час, в три часа ночи.
	  * (что, в общем, не верно, так как в Москве один раз перевод стрелок был осуществлён в другое время)
	  */

	inline time_t 		toTimeInaccurate(time_t t)		const
	{
		size_t index = findIndex(t);
		time_t day_length = lut[index + 1].date - lut[index].date;

		time_t res = t - lut[index].date;

		if (unlikely(day_length == 90000 && res >= 10800))		/// был произведён перевод стрелок назад
			res -= 3600;
		else if (unlikely(day_length == 82800 && res >= 7200))	/// был произведён перевод стрелок вперёд
			res += 3600;

		return res - offset_at_start_of_epoch; /// Отсчёт от 1970-01-01 00:00:00 по локальному времени
	}

	inline unsigned 	toHourInaccurate(time_t t)		const
	{
		size_t index = findIndex(t);
		time_t day_length = lut[index + 1].date - lut[index].date;
		unsigned res = (t - lut[index].date) / 3600;

		if (unlikely(day_length == 90000 && res >= 3))		/// был произведён перевод стрелок назад
			--res;
		else if (unlikely(day_length == 82800 && res >= 2))	/// был произведён перевод стрелок вперёд
			++res;

		return res;
	}

	inline unsigned 	toMinute(time_t t)	const {	return ((t - find(t).date) % 3600) / 60; }
	inline unsigned 	toSecond(time_t t)	const {	return (t - find(t).date) % 60; }

	inline unsigned 	toStartOfMinute(time_t t)	const
	{
		time_t date = find(t).date;
		return date + (t - date) / 60 * 60;
	}

	inline unsigned 	toStartOfHour(time_t t)		const
	{
		time_t date = find(t).date;
		return date + (t - date) / 3600 * 3600;
	}

	/** Только для часовых поясов, отличающихся от UTC на значение, кратное часу и без перевода стрелок не значение не кратное часу */

	inline unsigned 	toMinuteInaccurate(time_t t)		const {	return (t / 60) % 60; }
	inline unsigned 	toSecondInaccurate(time_t t)		const {	return t % 60; }

	inline unsigned 	toStartOfMinuteInaccurate(time_t t)		const {	return t / 60 * 60; }
	inline unsigned 	toStartOfFiveMinuteInaccurate(time_t t)	const {	return t / 300 * 300; }
	inline unsigned 	toStartOfHourInaccurate(time_t t)		const {	return t / 3600 * 3600; }

	/// Номер дня в пределах UNIX эпохи (и немного больше) - позволяет хранить дату в двух байтах

	inline DayNum_t		toDayNum(time_t t)			const { return static_cast<DayNum_t>(findIndex(t)); }
	inline time_t		fromDayNum(DayNum_t d)		const { return lut[fixDay(d)].date; }

	inline time_t 		toDate(DayNum_t d) 			const {	return lut[fixDay(d)].date; }
	inline unsigned 	toMonth(DayNum_t d) 		const {	return lut[fixDay(d)].month; }
	inline unsigned 	toYear(DayNum_t d) 			const {	return lut[fixDay(d)].year; }
	inline unsigned 	toDayOfWeek(DayNum_t d) 	const {	return lut[fixDay(d)].day_of_week; }
	inline unsigned 	toDayOfMonth(DayNum_t d)	const { return lut[fixDay(d)].day_of_month; }

	inline const Values & getValues(DayNum_t d)		const { return lut[fixDay(d)]; }
	inline const Values & getValues(time_t t)		const { return lut[findIndex(t)]; }

	/// получает DayNum_t из года, месяца, дня
	inline DayNum_t		makeDayNum(short year, char month, char day_of_month) const
	{
		if (unlikely(year < DATE_LUT_MIN_YEAR || year > DATE_LUT_MAX_YEAR || month < 1 || month > 12 || day_of_month < 1 || day_of_month > 31))
			return DayNum_t(0);
		DayNum_t any_day_of_month(years_lut[year - DATE_LUT_MIN_YEAR] + 31 * (month - 1));
		return DayNum_t(any_day_of_month - toDayOfMonth(any_day_of_month) + day_of_month);
	}

	inline time_t		makeDate(short year, char month, char day_of_month) const
	{
		return lut[makeDayNum(year, month, day_of_month)].date;
	}

	/** Функция ниже исходит из допущения, что перевод стрелок вперёд, если осуществляется, то на час, в два часа ночи,
	  *  а перевод стрелок назад, если осуществляется, то на час, в три часа ночи.
	  * (что, в общем, не верно, так как в Москве один раз перевод стрелок был осуществлён в другое время).
	  * Также, выдаётся лишь один из двух возможных вариантов при переводе стрелок назад.
	  */
	inline time_t		makeDateTime(short year, char month, char day_of_month, char hour, char minute, char second) const
	{
		size_t index = makeDayNum(year, month, day_of_month);
		time_t res = lut[index].date + hour * 3600 + minute * 60 + second;
		time_t day_length = lut[index + 1].date - lut[index].date;

		if (unlikely(day_length == 90000 && hour >= 3))			/// был произведён перевод стрелок назад
			res += 3600;
		else if (unlikely(day_length == 82800 && hour >= 2))	/// был произведён перевод стрелок вперёд
			res -= 3600;

		return res;
	}


	inline UInt32 toNumYYYYMMDD(time_t t) const
	{
		const Values & values = find(t);
		return values.year * 10000 + values.month * 100 + values.day_of_month;
	}

	inline UInt32 toNumYYYYMMDD(DayNum_t d) const
	{
		const Values & values = lut[fixDay(d)];
		return values.year * 10000 + values.month * 100 + values.day_of_month;
	}

	inline time_t YYYYMMDDToDate(UInt32 num) const
	{
		return makeDate(num / 10000, num / 100 % 100, num % 100);
	}

	inline DayNum_t YYYYMMDDToDayNum(UInt32 num) const
	{
		return makeDayNum(num / 10000, num / 100 % 100, num % 100);
	}


	inline UInt64 toNumYYYYMMDDhhmmss(time_t t) const
	{
		const Values & values = find(t);
		return
			  toSecondInaccurate(t)
			+ toMinuteInaccurate(t) 		* 100
			+ toHourInaccurate(t) 			* 10000
			+ UInt64(values.day_of_month)	* 1000000
			+ UInt64(values.month) 			* 100000000
			+ UInt64(values.year)			* 10000000000;
	}

	inline time_t YYYYMMDDhhmmssToTime(UInt64 num) const
	{
		return makeDateTime(
			num / 10000000000,
			num / 100000000 % 100,
			num / 1000000 % 100,
			num / 10000 % 100,
			num / 100 % 100,
			num % 100);
	}


	inline std::string timeToString(time_t t) const
	{
		const Values & values = find(t);

		std::string s {"0000-00-00 00:00:00"};

		s[0] += values.year / 1000;
		s[1] += (values.year / 100) % 10;
		s[2] += (values.year / 10) % 10;
		s[3] += values.year % 10;
		s[5] += values.month / 10;
		s[6] += values.month % 10;
		s[8] += values.day_of_month / 10;
		s[9] += values.day_of_month % 10;

		auto hour = toHourInaccurate(t);
		auto minute = toMinuteInaccurate(t);
		auto second = toSecondInaccurate(t);

		s[11] += hour / 10;
		s[12] += hour % 10;
		s[14] += minute / 10;
		s[15] += minute % 10;
		s[17] += second / 10;
		s[18] += second % 10;

		return s;
	}

	inline std::string dateToString(time_t t) const
	{
		const Values & values = find(t);

		std::string s {"0000-00-00"};

		s[0] += values.year / 1000;
		s[1] += (values.year / 100) % 10;
		s[2] += (values.year / 10) % 10;
		s[3] += values.year % 10;
		s[5] += values.month / 10;
		s[6] += values.month % 10;
		s[8] += values.day_of_month / 10;
		s[9] += values.day_of_month % 10;

		return s;
	}

	inline std::string dateToString(DayNum_t d) const
	{
		const Values & values = lut[fixDay(d)];

		std::string s {"0000-00-00"};

		s[0] += values.year / 1000;
		s[1] += (values.year / 100) % 10;
		s[2] += (values.year / 10) % 10;
		s[3] += values.year % 10;
		s[5] += values.month / 10;
		s[6] += values.month % 10;
		s[8] += values.day_of_month / 10;
		s[9] += values.day_of_month % 10;

		return s;
	}
};
