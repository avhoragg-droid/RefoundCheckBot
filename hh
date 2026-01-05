import os
import asyncio
import logging
import json
import sys
from datetime import datetime
from typing import Dict, List
from aiogram import Bot, Dispatcher, types
from aiogram.contrib.middlewares.logging import LoggingMiddleware
from aiogram.types import ParseMode, ChatType, InlineKeyboardMarkup, InlineKeyboardButton
from aiogram.utils import executor
from aiogram.contrib.fsm_storage.memory import MemoryStorage

# Конфигурация из переменных окружения
API_TOKEN = os.getenv('BOT_TOKEN', '').strip()
if not API_TOKEN:
    print("❌ Ошибка: BOT_TOKEN не установлен!")
    sys.exit(1)

# Получаем и преобразуем переменные окружения
try:
    CHANNEL_ID = int(os.getenv('CHANNEL_ID', '-1001234567890'))
    MODERATION_CHAT_ID = int(os.getenv('MODERATION_CHAT_ID', '-1003619015607'))
    LOG_CHANNEL_ID = int(os.getenv('LOG_CHANNEL_ID', '-1001987654321'))
    
    # Обрабатываем список администраторов
    admin_ids_str = os.getenv('ADMIN_IDS', '[7721644418]')
    ADMIN_IDS = json.loads(admin_ids_str)
    if not isinstance(ADMIN_IDS, list):
        ADMIN_IDS = [ADMIN_IDS]
except (ValueError, json.JSONDecodeError) as e:
    print(f"❌ Ошибка при парсинге переменных окружения: {e}")
    sys.exit(1)

# Директория для постоянного хранения данных
DATA_DIR = '/data'

# Настройка логирования
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.StreamHandler(sys.stdout)
    ]
)
logger = logging.getLogger(__name__)

# Инициализация бота
try:
    bot = Bot(token=API_TOKEN)
    storage = MemoryStorage()
    dp = Dispatcher(bot, storage=storage)
    dp.middleware.setup(LoggingMiddleware())
    logger.info("✅ Бот и диспетчер успешно инициализированы")
except Exception as e:
    logger.error(f"❌ Ошибка инициализации бота: {e}")
    sys.exit(1)

# Функции для работы с файлами в постоянном хранилище
def get_data_path(filename: str) -> str:
    """Получить путь к файлу в постоянном хранилище"""
    return os.path.join(DATA_DIR, filename)

def save_json(data, filename: str):
    """Сохранить данные в JSON файл в постоянном хранилище"""
    filepath = get_data_path(filename)
    try:
        with open(filepath, 'w', encoding='utf-8') as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        logger.debug(f"✅ Данные сохранены в {filename}")
    except Exception as e:
        logger.error(f"❌ Ошибка сохранения {filename}: {e}")

def load_json(filename: str, default=None):
    """Загрузить данные из JSON файла из постоянного хранилища"""
    filepath = get_data_path(filename)
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            data = json.load(f)
            logger.debug(f"✅ Данные загружены из {filename}")
            return data
    except FileNotFoundError:
        logger.debug(f"📭 Файл {filename} не найден, используется значение по умолчанию")
        return default if default is not None else {}
    except json.JSONDecodeError as e:
        logger.error(f"❌ Ошибка декодирования JSON в {filename}: {e}")
        return default if default is not None else {}
    except Exception as e:
        logger.error(f"❌ Ошибка загрузки {filename}: {e}")
        return default if default is not None else {}

# Хранилище данных
pending_messages: Dict[str, Dict] = {}
user_stats: Dict[int, Dict] = {}


def is_admin(user_id: int) -> bool:
    """Проверка, является ли пользователь администратором"""
    return user_id in ADMIN_IDS


async def log_action(action: str, user: types.User = None, data: dict = None):
    """Логирование действий"""
    log_entry = {
        'timestamp': datetime.now().isoformat(),
        'action': action,
        'user': {
            'id': user.id if user else None,
            'username': user.username if user else None,
            'full_name': user.full_name if user else None
        } if user else None,
        'data': data
    }
    
    logger.info(json.dumps(log_entry, ensure_ascii=False))
    
    if LOG_CHANNEL_ID:
        try:
            log_text = f"📝 <b>Лог:</b> {action}\n"
            if user:
                log_text += f"👤 <b>Пользователь:</b> {user.full_name}"
                if user.username:
                    log_text += f" (@{user.username})"
                log_text += f"\n🆔 <b>ID:</b> {user.id}"
            if data:
                log_text += f"\n📊 <b>Данные:</b> {json.dumps(data, ensure_ascii=False)[:200]}"
            
            await bot.send_message(LOG_CHANNEL_ID, log_text, parse_mode=ParseMode.HTML)
        except Exception as e:
            logger.error(f"Ошибка отправки лога: {e}")


async def send_to_channel(content_type: str, content: dict, caption: str = None):
    """Отправка контента в канал"""
    try:
        if content_type == 'text':
            message = await bot.send_message(CHANNEL_ID, content['text'])
        elif content_type == 'photo':
            message = await bot.send_photo(CHANNEL_ID, content['photo'], caption=caption)
        elif content_type == 'video':
            message = await bot.send_video(CHANNEL_ID, content['video'], caption=caption)
        elif content_type == 'document':
            message = await bot.send_document(CHANNEL_ID, content['document'], caption=caption)
        elif content_type == 'audio':
            message = await bot.send_audio(CHANNEL_ID, content['audio'], caption=caption)
        elif content_type == 'voice':
            message = await bot.send_voice(CHANNEL_ID, content['voice'])
        elif content_type == 'sticker':
            message = await bot.send_sticker(CHANNEL_ID, content['sticker'])
        else:
            return None
        
        logger.info(f"✅ Сообщение отправлено в канал, ID: {message.message_id}")
        return message.message_id
    except Exception as e:
        logger.error(f"❌ Ошибка отправки в канал: {e}")
        return None


async def send_moderation_request(user: types.User, content_type: str, content: dict, 
                                 caption: str = None, pending_id: str = None):
    """Отправка запроса на модерацию"""
    try:
        logger.info(f"📤 Отправка запроса на модерацию в чат {MODERATION_CHAT_ID}")
        
        moderation_text = f"🔄 <b>НОВОЕ СООБЩЕНИЕ НА МОДЕРАЦИЮ</b>\n\n"
        moderation_text += f"👤 <b>Отправитель:</b> {user.full_name}\n"
        if user.username:
            moderation_text += f"📱 <b>Username:</b> @{user.username}\n"
        moderation_text += f"🆔 <b>ID:</b> {user.id}\n"
        moderation_text += f"📄 <b>Тип:</b> {content_type}\n"
        moderation_text += f"🔑 <b>ID запроса:</b> <code>{pending_id}</code>\n"
        
        if content_type == 'text':
            text_preview = content['text'][:300]
            if len(content['text']) > 300:
                text_preview += "..."
            moderation_text += f"\n📝 <b>Текст:</b>\n{text_preview}"
        elif caption:
            caption_preview = caption[:300]
            if len(caption) > 300:
                caption_preview += "..."
            moderation_text += f"\n📝 <b>Подпись:</b>\n{caption_preview}"
        
        # Создаем клавиатуру для модерации
        keyboard = InlineKeyboardMarkup(row_width=2)
        keyboard.add(
            InlineKeyboardButton("✅ Одобрить", callback_data=f"approve_{pending_id}"),
            InlineKeyboardButton("❌ Отклонить", callback_data=f"reject_{pending_id}")
        )
        
        if content_type == 'text':
            keyboard.add(InlineKeyboardButton("👁‍🗨 Показать весь текст", callback_data=f"view_{pending_id}"))
        
        # Отправляем сообщение в чат модерации
        msg = None
        if content_type == 'text':
            msg = await bot.send_message(
                MODERATION_CHAT_ID,
                moderation_text,
                parse_mode=ParseMode.HTML,
                reply_markup=keyboard
            )
        elif content_type == 'photo':
            msg = await bot.send_photo(
                MODERATION_CHAT_ID,
                content.get('photo'),
                caption=moderation_text,
                parse_mode=ParseMode.HTML,
                reply_markup=keyboard
            )
        elif content_type == 'video':
            msg = await bot.send_video(
                MODERATION_CHAT_ID,
                content.get('video'),
                caption=moderation_text,
                parse_mode=ParseMode.HTML,
                reply_markup=keyboard
            )
        elif content_type == 'document':
            msg = await bot.send_document(
                MODERATION_CHAT_ID,
                content.get('document'),
                caption=moderation_text,
                parse_mode=ParseMode.HTML,
                reply_markup=keyboard
            )
        else:
            msg = await bot.send_message(
                MODERATION_CHAT_ID,
                moderation_text,
                parse_mode=ParseMode.HTML,
                reply_markup=keyboard
            )
        
        if msg:
            logger.info(f"✅ Сообщение модерации отправлено, ID: {msg.message_id}")
            return msg.message_id
        else:
            logger.error("❌ Не удалось отправить сообщение модерации")
            return None
        
    except Exception as e:
        logger.error(f"❌ Ошибка отправки запроса модерации: {e}")
        return None


async def forward_content(chat_id: int, content_type: str, content: dict, caption: str = None):
    """Пересылка контента в указанный чат"""
    try:
        if content_type == 'text':
            await bot.send_message(chat_id, content.get('text', ''))
        elif content_type == 'photo':
            await bot.send_photo(chat_id, content.get('photo'), caption=caption)
        elif content_type == 'video':
            await bot.send_video(chat_id, content.get('video'), caption=caption)
        elif content_type == 'document':
            await bot.send_document(chat_id, content.get('document'), caption=caption)
        elif content_type == 'audio':
            await bot.send_audio(chat_id, content.get('audio'), caption=caption)
        elif content_type == 'voice':
            await bot.send_voice(chat_id, content.get('voice'))
        elif content_type == 'sticker':
            await bot.send_sticker(chat_id, content.get('sticker'))
        logger.debug(f"✅ Контент переслан в чат {chat_id}")
    except Exception as e:
        logger.error(f"❌ Ошибка пересылки контента: {e}")


async def update_user_stats(user_id: int, action: str):
    """Обновление статистики пользователя"""
    try:
        if user_id not in user_stats:
            user_stats[user_id] = {
                'total_messages': 0,
                'approved': 0,
                'rejected': 0,
                'pending': 0,
                'last_activity': datetime.now().isoformat()
            }
        
        user_stats[user_id]['last_activity'] = datetime.now().isoformat()
        
        if action == 'sent':
            user_stats[user_id]['total_messages'] += 1
            user_stats[user_id]['pending'] += 1
        elif action == 'approved':
            user_stats[user_id]['approved'] += 1
            if user_stats[user_id]['pending'] > 0:
                user_stats[user_id]['pending'] -= 1
        elif action == 'rejected':
            user_stats[user_id]['rejected'] += 1
            if user_stats[user_id]['pending'] > 0:
                user_stats[user_id]['pending'] -= 1
        
        # Автосохранение статистики
        save_json(user_stats, 'user_stats.json')
    except Exception as e:
        logger.error(f"❌ Ошибка обновления статистики пользователя {user_id}: {e}")


@dp.message_handler(commands=['start'])
async def cmd_start(message: types.Message):
    """Команда старт"""
    user = message.from_user
    await log_action("start_command", user)
    
    if is_admin(user.id):
        text = "👑 <b>Режим администратора</b>\n\n"
        text += "Доступные команды:\n"
        text += "/publish - Опубликовать сообщение в канал (ответьте на сообщение)\n"
        text += "/stats - Статистика\n"
        text += "/moderate - Показать ожидающие сообщения\n"
        text += "/users - Статистика пользователей\n"
        text += "/addadmin - Добавить администратора\n"
        text += "/removeadmin - Удалить администратора\n"
        text += "/listadmins - Список администраторов\n"
        text += "/help - Помощь"
    else:
        text = "👋 <b>Добро пожаловать!</b>\n\n"
        text += "Отправьте любое сообщение (текст, фото, видео и т.д.), "
        text += "и оно будет отправлено администраторам на модерацию.\n\n"
        text += "Администраторы проверят ваше сообщение и опубликуют его в канале.\n\n"
        text += "📊 Ваша статистика: /mystats\n"
        text += "📋 Помощь: /help"
    
    await message.reply(text, parse_mode=ParseMode.HTML)


@dp.message_handler(commands=['help'])
async def cmd_help(message: types.Message):
    """Помощь по командам"""
    user = message.from_user
    await log_action("help_command", user)
    
    if is_admin(user.id):
        text = "📋 <b>Помощь по командам (администратор)</b>\n\n"
        text += "/publish - Опубликовать сообщение в канал (ответьте на сообщение)\n"
        text += "/stats - Статистика системы\n"
        text += "/moderate - Сообщения на модерации\n"
        text += "/users - Список пользователей\n"
        text += "/addadmin ID - Добавить администратора\n"
        text += "/removeadmin ID - Удалить администратора\n"
        text += "/listadmins - Список администраторов\n"
        text += "/mystats - Ваша статистика"
    else:
        text = "📋 <b>Помощь по командам</b>\n\n"
        text += "Просто отправьте любое сообщение (текст, фото, видео и т.д.) "
        text += "и оно будет отправлено администраторам на проверку.\n\n"
        text += "После одобрения администратором ваше сообщение будет опубликовано в канале.\n\n"
        text += "/mystats - Ваша статистика\n"
        text += "/start - Основная информация"
    
    await message.reply(text, parse_mode=ParseMode.HTML)


@dp.message_handler(commands=['mystats'])
async def cmd_mystats(message: types.Message):
    """Статистика пользователя"""
    user = message.from_user
    await log_action("my_stats_command", user)
    
    stats = user_stats.get(user.id, {})
    
    text = f"📊 <b>Ваша статистика</b>\n\n"
    text += f"👤 <b>Имя:</b> {user.full_name}\n"
    if user.username:
        text += f"📱 <b>Username:</b> @{user.username}\n"
    text += f"🆔 <b>ID:</b> {user.id}\n\n"
    
    if stats:
        text += f"📨 <b>Всего отправлено:</b> {stats.get('total_messages', 0)}\n"
        text += f"✅ <b>Одобрено:</b> {stats.get('approved', 0)}\n"
        text += f"❌ <b>Отклонено:</b> {stats.get('rejected', 0)}\n"
        text += f"⏳ <b>Ожидает модерации:</b> {stats.get('pending', 0)}\n"
        
        if 'last_activity' in stats:
            last_activity = datetime.fromisoformat(stats['last_activity'])
            text += f"🕐 <b>Последняя активность:</b> {last_activity.strftime('%d.%m.%Y %H:%M')}"
    else:
        text += "📭 Вы еще не отправляли сообщений"
    
    await message.reply(text, parse_mode=ParseMode.HTML)


@dp.message_handler(commands=['stats'])
async def cmd_stats(message: types.Message):
    """Статистика системы (только для админов)"""
    user = message.from_user
    await log_action("stats_command", user)
    
    if not is_admin(user.id):
        await message.reply("⛔ Недостаточно прав!")
        return
    
    total_users = len(user_stats)
    total_messages = sum([stats.get('total_messages', 0) for stats in user_stats.values()])
    total_pending = sum([stats.get('pending', 0) for stats in user_stats.values()])
    
    text = f"📊 <b>Статистика системы</b>\n\n"
    text += f"👥 <b>Всего пользователей:</b> {total_users}\n"
    text += f"📨 <b>Всего сообщений:</b> {total_messages}\n"
    text += f"⏳ <b>Ожидает модерации:</b> {total_pending}\n"
    text += f"👑 <b>Администраторов:</b> {len(ADMIN_IDS)}\n"
    text += f"📢 <b>ID канала:</b> {CHANNEL_ID}\n"
    text += f"⚖️ <b>Чат модерации:</b> {MODERATION_CHAT_ID}"
    
    await message.reply(text, parse_mode=ParseMode.HTML)


@dp.message_handler(commands=['users'])
async def cmd_users(message: types.Message):
    """Список пользователей (только для админов)"""
    user = message.from_user
    await log_action("users_command", user)
    
    if not is_admin(user.id):
        await message.reply("⛔ Недостаточно прав!")
        return
    
    if not user_stats:
        await message.reply("📭 Пользователей нет")
        return
    
    text = "👥 <b>Активные пользователи:</b>\n\n"
    
    for user_id, stats in list(user_stats.items())[:20]:
        text += f"• ID: {user_id}\n"
        text += f"  📨: {stats.get('total_messages', 0)} | "
        text += f"✅: {stats.get('approved', 0)} | "
        text += f"❌: {stats.get('rejected', 0)} | "
        text += f"⏳: {stats.get('pending', 0)}\n"
    
    if len(user_stats) > 20:
        text += f"\n... и еще {len(user_stats) - 20} пользователей"
    
    await message.reply(text, parse_mode=ParseMode.HTML)


@dp.message_handler(commands=['moderate'])
async def cmd_moderate(message: types.Message):
    """Показать ожидающие модерации сообщения (только для админов)"""
    user = message.from_user
    await log_action("moderate_command", user)
    
    if not is_admin(user.id):
        await message.reply("⛔ Недостаточно прав!")
        return
    
    if not pending_messages:
        await message.reply("✅ Нет сообщений, ожидающих модерации")
        return
    
    text = f"⏳ <b>Сообщения на модерации:</b> {len(pending_messages)}\n\n"
    
    for pending_id, msg_data in list(pending_messages.items())[:10]:
        user_info = msg_data.get('user', {})
        text += f"📝 <b>ID запроса:</b> <code>{pending_id}</code>\n"
        text += f"👤 <b>От:</b> {user_info.get('full_name', 'Unknown')}\n"
        text += f"🕐 <b>Время:</b> {msg_data.get('timestamp', 'Unknown')}\n"
        text += f"📄 <b>Тип:</b> {msg_data.get('content_type', 'Unknown')}\n"
        
        content_preview = ""
        if msg_data['content_type'] == 'text':
            content_preview = msg_data.get('content', {}).get('text', '')[:50]
        elif msg_data.get('caption'):
            content_preview = msg_data['caption'][:50]
        
        if content_preview:
            text += f"📋 <b>Контент:</b> {content_preview}...\n"
        
        # Кнопки для этого конкретного сообщения
        keyboard = InlineKeyboardMarkup(row_width=2)
        keyboard.add(
            InlineKeyboardButton("✅ Одобрить", callback_data=f"approve_{pending_id}"),
            InlineKeyboardButton("❌ Отклонить", callback_data=f"reject_{pending_id}")
        )
        
        await message.reply(text, parse_mode=ParseMode.HTML, reply_markup=keyboard)
        text = ""  # Сбрасываем текст для следующего сообщения
    
    if len(pending_messages) > 10:
        await message.reply(f"... и еще {len(pending_messages) - 10} сообщений")


@dp.message_handler(commands=['publish'])
async def cmd_publish(message: types.Message):
    """Публикация сообщения в канал (только для админов)"""
    user = message.from_user
    
    if not is_admin(user.id):
        await message.reply("⛔ Недостаточно прав! Только администраторы могут публиковать сообщения.")
        return
    
    if message.reply_to_message:
        # Если команда отправлена в ответ на сообщение
        await log_action("publish_command_reply", user)
        
        # Извлекаем контент из сообщения, на которое ответили
        content_type, content, caption = await extract_content(message.reply_to_message)
        
        if not content_type:
            await message.reply("❌ Неподдерживаемый тип контента!")
            return
        
        # Отправляем в канал
        channel_msg_id = await send_to_channel(content_type, content, caption)
        
        if channel_msg_id:
            await message.reply("✅ Сообщение опубликовано в канал!")
            
            # Логируем публикацию
            await log_action("message_published_via_command", user, {
                'content_type': content_type,
                'channel_msg_id': channel_msg_id
            })
        else:
            await message.reply("❌ Ошибка при публикации в канал!")
    else:
        # Если команда отправлена без реплая
        await log_action("publish_command_no_reply", user)
        
        # Объясняем, как использовать команду
        await message.reply(
            "📝 <b>Как использовать команду /publish:</b>\n\n"
            "Ответьте командой /publish на сообщение, которое хотите опубликовать в канал.\n\n"
            "Это может быть:\n"
            "1. Сообщение от пользователя в этом чате\n"
            "2. Сообщение из чата модерации\n"
            "3. Любое другое сообщение, которое вы хотите опубликовать",
            parse_mode=ParseMode.HTML
        )


@dp.message_handler(commands=['addadmin'], chat_type=ChatType.PRIVATE)
async def cmd_addadmin(message: types.Message):
    """Добавление администратора (только для админов)"""
    user = message.from_user
    await log_action("addadmin_command", user)
    
    if not is_admin(user.id):
        await message.reply("⛔ Недостаточно прав!")
        return
    
    try:
        if message.reply_to_message:
            new_admin_id = message.reply_to_message.from_user.id
        else:
            args = message.get_args()
            if not args:
                await message.reply("❌ Укажите ID пользователя или ответьте на его сообщение")
                return
            new_admin_id = int(args)
        
        if new_admin_id in ADMIN_IDS:
            await message.reply("⚠ Этот пользователь уже администратор")
            return
        
        ADMIN_IDS.append(new_admin_id)
        
        # Сохраняем в постоянное хранилище
        save_json(ADMIN_IDS, 'admins.json')
        
        await message.reply(f"✅ Пользователь {new_admin_id} добавлен в администраторы")
            
    except ValueError:
        await message.reply("❌ Неверный формат ID")
    except Exception as e:
        await message.reply(f"❌ Ошибка: {e}")


@dp.message_handler(commands=['removeadmin'], chat_type=ChatType.PRIVATE)
async def cmd_removeadmin(message: types.Message):
    """Удаление администратора (только для админов)"""
    user = message.from_user
    await log_action("removeadmin_command", user)
    
    if not is_admin(user.id):
        await message.reply("⛔ Недостаточно прав!")
        return
    
    try:
        args = message.get_args()
        if not args:
            await message.reply("❌ Укажите ID администратора")
            return
        
        admin_id = int(args)
        if admin_id not in ADMIN_IDS:
            await message.reply("❌ Пользователь не найден в списке администраторов")
            return
        
        if admin_id == user.id:
            await message.reply("❌ Вы не можете удалить себя!")
            return
        
        ADMIN_IDS.remove(admin_id)
        
        # Сохраняем в постоянное хранилище
        save_json(ADMIN_IDS, 'admins.json')
        
        await message.reply(f"✅ Пользователь {admin_id} удален из администраторов")
            
    except ValueError:
        await message.reply("❌ Неверный формат ID")
    except Exception as e:
        await message.reply(f"❌ Ошибка: {e}")


@dp.message_handler(commands=['listadmins'])
async def cmd_listadmins(message: types.Message):
    """Список администраторов (только для админов)"""
    user = message.from_user
    await log_action("listadmins_command", user)
    
    if not is_admin(user.id):
        await message.reply("⛔ Недостаточно прав!")
        return
    
    if not ADMIN_IDS:
        await message.reply("📭 Список администраторов пуст")
        return
    
    admins_list = "\n".join([f"• {admin_id}" for admin_id in ADMIN_IDS])
    await message.reply(f"📋 <b>Список администраторов:</b>\n{admins_list}", parse_mode=ParseMode.HTML)


# Обработчик для неизвестных команд
@dp.message_handler(lambda message: message.text and message.text.startswith('/'))
async def unknown_command(message: types.Message):
    """Обработка неизвестных команд"""
    user = message.from_user
    
    # Проверяем известные команды
    known_commands = ['start', 'help', 'mystats', 'stats', 'users', 'moderate', 
                      'publish', 'addadmin', 'removeadmin', 'listadmins']
    
    command = message.text.split()[0][1:]  # Убираем первый символ '/'
    
    if command.split('@')[0] not in known_commands:  # Игнорируем username бота если есть
        await log_action("unknown_command", user, {'command': command})
        
        if is_admin(user.id):
            error_text = f"❌ <b>Неизвестная команда:</b> /{command}\n\n"
            error_text += "📋 <b>Доступные команды (администратор):</b>\n"
            error_text += "/publish - Опубликовать сообщение в канал\n"
            error_text += "/stats - Статистика\n"
            error_text += "/moderate - Сообщения на модерации\n"
            error_text += "/users - Список пользователей\n"
            error_text += "/help - Помощь"
        else:
            error_text = f"❌ <b>Неизвестная команда:</b> /{command}\n\n"
            error_text += "📋 <b>Доступные команды:</b>\n"
            error_text += "/start - Начало работы\n"
            error_text += "/help - Помощь\n"
            error_text += "/mystats - Ваша статистика"
        
        await message.reply(error_text, parse_mode=ParseMode.HTML)


@dp.callback_query_handler(lambda c: c.data.startswith('approve_'))
async def process_approval(callback_query: types.CallbackQuery):
    """Обработка одобрения сообщения (только для админов)"""
    user = callback_query.from_user
    
    logger.info(f"APPROVE CALLBACK: {callback_query.data} от {user.id}")
    
    if not is_admin(user.id):
        await callback_query.answer("⛔ Недостаточно прав!", show_alert=True)
        return
    
    pending_id = callback_query.data.split('_', 1)[1]
    msg_data = pending_messages.get(pending_id)
    
    if not msg_data:
        await callback_query.answer("❌ Сообщение не найдено!", show_alert=True)
        return
    
    # Отправляем в канал
    channel_msg_id = await send_to_channel(
        msg_data['content_type'],
        msg_data['content'],
        msg_data.get('caption')
    )
    
    if channel_msg_id:
        # Уведомляем отправителя
        sender_id = msg_data['user']['id']
        try:
            await bot.send_message(
                sender_id,
                f"✅ <b>Ваше сообщение одобрено!</b>\n\n"
                f"Администратор одобрил публикацию вашего сообщения в канале.",
                parse_mode=ParseMode.HTML
            )
        except Exception as e:
            logger.error(f"Не удалось уведомить пользователя {sender_id}: {e}")
        
        # Обновляем статистику
        await update_user_stats(sender_id, 'approved')
        
        # Удаляем из ожидающих и сохраняем
        if pending_id in pending_messages:
            del pending_messages[pending_id]
            save_json(pending_messages, 'pending_messages.json')
        
        # Обновляем сообщение модерации
        try:
            await callback_query.message.edit_text(
                f"{callback_query.message.text}\n\n"
                f"✅ <b>ОДОБРЕНО</b> администратором: {user.full_name}\n"
                f"🕐 Время: {datetime.now().strftime('%H:%M:%S')}",
                parse_mode=ParseMode.HTML
            )
        except Exception as e:
            logger.error(f"Не удалось обновить сообщение: {e}")
        
        await callback_query.answer("✅ Сообщение одобрено!")
        
        await log_action("message_approved", user, {
            'pending_id': pending_id,
            'sender_id': sender_id,
            'channel_msg_id': channel_msg_id
        })
    else:
        await callback_query.answer("❌ Ошибка отправки в канал!", show_alert=True)


@dp.callback_query_handler(lambda c: c.data.startswith('reject_'))
async def process_rejection(callback_query: types.CallbackQuery):
    """Обработка отклонения сообщения (только для админов)"""
    user = callback_query.from_user
    
    logger.info(f"REJECT CALLBACK: {callback_query.data} от {user.id}")
    
    if not is_admin(user.id):
        await callback_query.answer("⛔ Недостаточно прав!", show_alert=True)
        return
    
    pending_id = callback_query.data.split('_', 1)[1]
    msg_data = pending_messages.get(pending_id)
    
    if not msg_data:
        await callback_query.answer("❌ Сообщение не найдено!", show_alert=True)
        return
    
    # Уведомляем отправителя
    sender_id = msg_data['user']['id']
    try:
        await bot.send_message(
            sender_id,
            f"❌ <b>Ваше сообщение отклонено</b>\n\n"
            f"Администратор не одобрил публикацию вашего сообщения.",
            parse_mode=ParseMode.HTML
        )
    except Exception as e:
        logger.error(f"Не удалось уведомить пользователя {sender_id}: {e}")
    
    # Обновляем статистику
    await update_user_stats(sender_id, 'rejected')
    
    # Удаляем из ожидающих и сохраняем
    if pending_id in pending_messages:
        del pending_messages[pending_id]
        save_json(pending_messages, 'pending_messages.json')
    
    # Обновляем сообщение модерации
    try:
        await callback_query.message.edit_text(
            f"{callback_query.message.text}\n\n"
            f"❌ <b>ОТКЛОНЕНО</b> администратором: {user.full_name}\n"
            f"🕐 Время: {datetime.now().strftime('%H:%M:%S')}",
            parse_mode=ParseMode.HTML
        )
    except Exception as e:
        logger.error(f"Не удалось обновить сообщение: {e}")
    
    await callback_query.answer("❌ Сообщение отклонено!")
    
    await log_action("message_rejected", user, {
        'pending_id': pending_id,
        'sender_id': sender_id
    })


@dp.callback_query_handler(lambda c: c.data.startswith('view_'))
async def process_view(callback_query: types.CallbackQuery):
    """Просмотр полного текста сообщения (только для админов)"""
    user = callback_query.from_user
    
    logger.info(f"VIEW CALLBACK: {callback_query.data} от {user.id}")
    
    if not is_admin(user.id):
        await callback_query.answer("⛔ Недостаточно прав!", show_alert=True)
        return
    
    pending_id = callback_query.data.split('_', 1)[1]
    msg_data = pending_messages.get(pending_id)
    
    if not msg_data:
        await callback_query.answer("❌ Сообщение не найдено!", show_alert=True)
        return
    
    # Показываем полный текст только для текстовых сообщений
    if msg_data['content_type'] == 'text':
        full_text = msg_data['content'].get('text', '')
        if len(full_text) > 4000:
            full_text = full_text[:4000] + "..."
        
        await callback_query.answer(full_text, show_alert=True)
    else:
        await callback_query.answer("ℹ️ Полный текст доступен только для текстовых сообщений", show_alert=True)


async def extract_content(message: types.Message):
    """Извлечение контента из сообщения"""
    content_type = None
    content = {}
    caption = None
    
    if message.text and not message.text.startswith('/'):
        content_type = 'text'
        content = {'text': message.text}
    elif message.photo:
        content_type = 'photo'
        content = {'photo': message.photo[-1].file_id}
        caption = message.caption
    elif message.video:
        content_type = 'video'
        content = {'video': message.video.file_id}
        caption = message.caption
    elif message.document:
        content_type = 'document'
        content = {'document': message.document.file_id}
        caption = message.caption
    elif message.audio:
        content_type = 'audio'
        content = {'audio': message.audio.file_id}
        caption = message.caption
    elif message.voice:
        content_type = 'voice'
        content = {'voice': message.voice.file_id}
    elif message.sticker:
        content_type = 'sticker'
        content = {'sticker': message.sticker.file_id}
    elif message.text and message.text.startswith('/'):
        # Это команда, пропускаем
        return None, None, None
    
    return content_type, content, caption


@dp.message_handler(content_types=types.ContentType.ANY)
async def handle_message(message: types.Message):
    """Обработка всех типов сообщений (кроме команд)"""
    user = message.from_user
    
    # Извлекаем контент
    content_type, content, caption = await extract_content(message)
    
    if not content_type:
        # Это команда или неподдерживаемый тип контента
        return
    
    if is_admin(user.id):
        # Администраторы могут отправлять сообщения напрямую в канал
        channel_msg_id = await send_to_channel(content_type, content, caption)
        
        if channel_msg_id:
            await message.reply("✅ Сообщение отправлено в канал!")
            
            await log_action("admin_message_sent_to_channel", user, {
                'content_type': content_type,
                'channel_msg_id': channel_msg_id
            })
        else:
            await message.reply("❌ Ошибка при отправке в канал!")
    else:
        # Обычные пользователи отправляют сообщения на модерацию
        await update_user_stats(user.id, 'sent')
        
        # Логируем получение сообщения
        await log_action("message_received_for_moderation", user, {
            'content_type': content_type,
            'has_caption': bool(caption)
        })
        
        # Создаем уникальный ID для сообщения
        pending_id = f"{user.id}_{message.message_id}_{int(datetime.now().timestamp())}"
        
        pending_messages[pending_id] = {
            'user': {
                'id': user.id,
                'username': user.username,
                'full_name': user.full_name
            },
            'content_type': content_type,
            'content': content,
            'caption': caption,
            'timestamp': datetime.now().strftime('%H:%M:%S'),
            'original_message_id': message.message_id
        }
        
        # Сохраняем в постоянное хранилище
        save_json(pending_messages, 'pending_messages.json')
        
        logger.info(f"Создан pending_id: {pending_id}")
        
        # Отправляем запрос на модерацию
        moderation_msg_id = await send_moderation_request(
            user, content_type, content, caption, pending_id
        )
        
        if moderation_msg_id:
            await message.reply(
                "✅ Сообщение отправлено администраторам на модерацию!\n\n"
                "Администраторы проверят ваше сообщение и, если оно соответствует правилам, "
                "опубликуют его в канале.\n\n"
                "Ожидайте уведомления о статусе модерации."
            )
            
            await log_action("message_sent_for_moderation", user, {
                'content_type': content_type,
                'pending_id': pending_id,
                'moderation_msg_id': moderation_msg_id
            })
        else:
            await message.reply("❌ Ошибка при отправке на модерацию! Попробуйте позже.")
            # Удаляем из ожидающих, если не удалось отправить на модерацию
            if pending_id in pending_messages:
                del pending_messages[pending_id]
                save_json(pending_messages, 'pending_messages.json')


async def on_startup(dp):
    """Действия при запуске бота"""
    logger.info("=" * 50)
    logger.info("🚀 ЗАПУСК БОТА")
    logger.info("=" * 50)
    
    # Создаем директорию /data если она не существует
    os.makedirs(DATA_DIR, exist_ok=True)
    logger.info(f"📁 Создана директория данных: {DATA_DIR}")
    
    # Загружаем данные из постоянного хранилища
    global user_stats, pending_messages, ADMIN_IDS
    
    logger.info("📂 Загрузка данных из постоянного хранилища...")
    user_stats = load_json('user_stats.json', {})
    pending_messages = load_json('pending_messages.json', {})
    
    # Конвертируем ключи user_stats обратно в int
    if user_stats:
        try:
            user_stats = {int(k): v for k, v in user_stats.items()}
            logger.info(f"✅ Загружена статистика {len(user_stats)} пользователей")
        except ValueError as e:
            logger.error(f"❌ Ошибка конвертации ключей user_stats: {e}")
            user_stats = {}
    
    logger.info(f"✅ Загружено {len(pending_messages)} сообщений на модерации")
    
    # Загружаем администраторов
    loaded_admins = load_json('admins.json', ADMIN_IDS)
    ADMIN_IDS.clear()
    if isinstance(loaded_admins, list):
        ADMIN_IDS.extend(loaded_admins)
    elif loaded_admins:
        ADMIN_IDS.append(loaded_admins)
    
    # Логируем информацию о конфигурации
    logger.info("⚙️ Конфигурация бота:")
    logger.info(f"   • CHANNEL_ID: {CHANNEL_ID}")
    logger.info(f"   • MODERATION_CHAT_ID: {MODERATION_CHAT_ID}")
    logger.info(f"   • LOG_CHANNEL_ID: {LOG_CHANNEL_ID}")
    logger.info(f"   • Администраторов: {len(ADMIN_IDS)}")
    logger.info(f"   • ID администраторов: {ADMIN_IDS}")
    
    # Уведомляем администраторов
    for admin_id in ADMIN_IDS:
        try:
            await bot.send_message(
                admin_id,
                "🤖 <b>Бот запущен!</b>\n\n"
                f"🕐 Время: {datetime.now().strftime('%d.%m.%Y %H:%M:%S')}\n"
                f"👥 Пользователей: {len(user_stats)}\n"
                f"⏳ Сообщений на модерации: {len(pending_messages)}\n"
                f"📢 Канал: {CHANNEL_ID}\n\n"
                f"<b>Вы являетесь администратором.</b>",
                parse_mode=ParseMode.HTML
            )
            logger.info(f"✅ Администратор {admin_id} уведомлен")
        except Exception as e:
            logger.error(f"❌ Не удалось уведомить администратора {admin_id}: {e}")
    
    logger.info("✅ Бот успешно запущен и готов к работе!")


async def on_shutdown(dp):
    """Действия при остановке бота"""
    logger.info("=" * 50)
    logger.info("🛑 ОСТАНОВКА БОТА")
    logger.info("=" * 50)
    
    # Сохраняем данные в постоянное хранилище
    logger.info("💾 Сохранение данных...")
    save_json(user_stats, 'user_stats.json')
    save_json(pending_messages, 'pending_messages.json')
    save_json(ADMIN_IDS, 'admins.json')
    
    logger.info("✅ Все данные сохранены")
    logger.info("👋 Бот остановлен")


if __name__ == '__main__':
    try:
        logger.info("Запуск бота...")
        executor.start_polling(
            dp, 
            skip_updates=True,
            on_startup=on_startup,
            on_shutdown=on_shutdown
        )
    except Exception as e:
        logger.error(f"❌ Критическая ошибка при запуске бота: {e}")
        sys.exit(1)