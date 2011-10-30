#pragma once

#include <DB/Interpreters/Context.h>


namespace DB
{


/** Загружает определения таблиц и добавляет их в контекст.
  */
void loadMetadata(Context & context);


}
