#pragma once

#include <DB/Core/Row.h>


namespace DB
{

/** Интерфейс потока для записи данных по строкам (например, для вывода в консоль).
  */
class IRowOutputStream
{
public:

	/** Записать строку.
	  * Есть реализация по умолчанию, которая использует методы для записи одиночных значений и разделителей
	  * (кроме разделителя между строк (writeRowBetweenDelimiter())).
	  */
	virtual void write(const Row & row);

	/** Записать значение. */
	virtual void writeField(const Field & field) = 0;

	/** Записать разделитель. */
	virtual void writeFieldDelimiter() {};
	virtual void writeRowStartDelimiter() {};
	virtual void writeRowEndDelimiter() {};
	virtual void writeRowBetweenDelimiter() {};

	/** Создать копию объекта.
	  * Предполагается, что функция вызывается только до использования объекта (сразу после создания, до вызова других методов),
	  *  только для того, чтобы можно было преобразовать параметр, переданный по ссылке в shared ptr.
	  */
	virtual SharedPtr<IRowOutputStream> clone() = 0;

	virtual ~IRowOutputStream() {}
};

typedef SharedPtr<IRowOutputStream> RowOutputStreamPtr;

}
