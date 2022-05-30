#pragma once

#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <algorithm>   // для copy_n
#include <type_traits>  // для constexpr is_*-функций

/*
Согласно идиоме RAII, жизненный цикл ресурса, который программа 
получает во временное пользование, должен привязываться ко времени 
жизни объекта.Создав объект, который владеет некоторым ресурсом, 
программа может использовать этот ресурс на протяжении жизни объекта.

Класс Vector владеет несколькими типами ресурсов :
-сырая память, которую класс запрашивает, используя Allocate, и освобождает, используя Deallocate;
-элементы вектора, которые создаются размещающим оператором new и удаляются вызовом деструктора.

Выделив код, управляющий сырой памятью, в отдельный класс - обёртку, 
можно упростить класс Vector.
Шаблонный класс RawMemory будет отвечать за хранение буфера, который 
вмещает заданное количество элементов, и предоставлять доступ к элементам по индексу
*/
template <typename T>
class RawMemory
{
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity)
    {}

    /*
    Операция копирования не имеет смысла для класса RawMemory, так как у него 
    нет информации о количестве находящихся в сырой памяти элементов. 
    Копирование элементов определено для класса Vector, который использует 
    сырую память для размещения элементов и знает об их количестве.
    Поэтому конструктор копирования и копирующий оператор присваивания 
    в классе RawMemory должны быть запрещены.
    */
    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;

   /*
   Перемещающие конструктор и оператор присваивания не выбрасывают 
   исключений и выполняются за O(1).
   */
    RawMemory(RawMemory&& other) noexcept
    {
        Swap(other);
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept
    {
        Swap(rhs);
        return *this;
    }

    ~RawMemory()
    {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept
    {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept
    {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept
    {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept
    {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept
    {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept
    {
        return buffer_;
    }

    T* GetAddress() noexcept
    {
        return buffer_;
    }

    size_t Capacity() const
    {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n)
    {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept
    {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};


template <typename T>
class Vector
{
public:
    using iterator = T*;
    using const_iterator = const T*;

    iterator begin() noexcept
    {
        return data_.GetAddress();
    }

    iterator end() noexcept
    {
        return data_.GetAddress() + size_;
    }

    const_iterator begin() const noexcept
    {
        return data_.GetAddress();
    }

    const_iterator end() const noexcept
    {
        return data_.GetAddress() + size_;
    }

    const_iterator cbegin() const noexcept
    {
        return data_.GetAddress();
    }

    const_iterator cend() const noexcept
    {
        return data_.GetAddress() + size_;
    }


    Vector() = default;

    explicit Vector(size_t size)
        : data_(size)
        , size_(size)  //
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    /*
    Чтобы создать копию контейнера Vector, выделим память под нужное 
    количество элементов, а затем сконструируем в ней копию элементов 
    оригинального контейнера, используя функцию CopyConstruct
    Заменено на: std::uninitialized_copy_n
    */
    Vector(const Vector& other)
        : data_(other.size_)
        , size_(other.size_)  //
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
    }

    /*
    Move-конструктор класса Vector легко написать, если использовать соответствующий 
    конструктор RawMemory. После перемещения новый вектор станет владеть данными 
    исходного вектора. Исходный вектор будет иметь нулевой размер и вместимость 
    и ссылаться на nullptr.
    */
    Vector(Vector&& other) noexcept
    {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    ~Vector()
    {
        std::destroy_n(data_.GetAddress(), size_);
    }

    // Оператор присваивания, основанный на идиоме copy-and-swap
    Vector& operator=(const Vector& rhs)
    {
        if (this != &rhs)
        {
            // Более оптимальная стратегия — применять copy-and-swap только 
            // когда вместимости вектора-приёмника не хватает, чтобы вместить 
            // все элементы вектора-источника
            if (data_.Capacity() < rhs.size_)
            {
                // capacity источника (rhs) больше емкости приемника.
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            }
            else
            {
                // capacity источника (rhs) меньше или равен емкости приемника. Избегаем лишнего копирования.
                if (size_ > rhs.size_)
                {
                    // В текущем векторе элементов больше, чем в rhs
                    std::copy_n(rhs.data_.GetAddress(), rhs.size_, data_.GetAddress());
                    std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
                }
                else
                {
                    // В текущем векторе элементов меньше или равно, чем в rhs
                    std::copy_n(rhs.data_.GetAddress(), size_, data_.GetAddress());
                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_,
                                              rhs.size_ - size_,
                                              data_.GetAddress() + size_);
                }
                size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept
    {
        if (this != &rhs)
        {
            Swap(rhs);
        }
        return *this;
    }

    // Метод Resize должен предоставлять строгую гарантию безопасности исключений, 
    // когда выполняется любое из условий:
    // -move-конструктор у типа T объявлен как noexcept;
    // -тип T имеет публичный конструктор копирования.
    // Если у типа T нет конструктора копирования и move-конструктор может 
    // выбрасывать исключения, метод Resize может предоставлять базовую или 
    // строгую гарантию безопасности исключений.
    // Сложность метода Resize должна линейно зависеть от разницы между текущим 
    // и новым размером вектора. Если новый размер превышает текущую вместимость вектора,
    // сложность операции может дополнительно линейно зависеть от текущего размера вектора.
    void Resize(size_t new_size)
    {
        if (new_size > size_)
        {
            // увеличиваем размер и инициализируем добавленные элементы
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
            size_ = new_size;
        }
        else if (new_size < size_)
        {
            // уменьшаем размер, удаляя лишнее
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
            size_ = new_size;
        }
        // Если new_size == size_, то ничего не делаем
    }


    // Метод EmplaceBack должен предоставлять строгую гарантию безопасности 
    // исключений, когда выполняется любое из условий :
    // -move - конструктор у типа T объявлен как noexcept;
    // -тип T имеет публичный конструктор копирования.
    // Если у типа T нет конструктора копирования и move - конструктор может 
    // выбрасывать исключения, метод EmplaceBack должен предоставлять базовую 
    // гарантию безопасности исключений.
    // Сложность метода EmplaceBack должна быть амортизированной константой.
    template <class... Args>
    T& EmplaceBack(Args&&... args)
    {
        // Метод EmplaceBack может вызвать любые конструкторы типа T, в том числе объявленные 
        // explicit. В некоторых случаях это может создать проблемы, которые были бы невозможны 
        // при использовании PushBack. Например, когда внутри вектора хранятся указатели unique_ptr

        // Есть ли место по новый элемент?
        if (size_ < Capacity())
        {
            // Место есть. Память пока не распределена. 
            // Вызываем конструктор непосредственно в буфере
            new(data_ + size_) T(std::forward<Args>(args)...);
            ++size_;
        }
        else
        {
            // Места больше нет. Выделяем
            size_t new_capacity = (size_ == 0 ? 1 : size_ * 2);
            RawMemory<T> new_data(new_capacity);
            // Создаем новый элемент в новом буфере
            new(new_data + size_) T(std::forward<Args>(args)...);
            // Есть ли у типа noexcept-конструктор перемещения ИЛИ отсутствует копирующий конструктор (т.е. есть move-конструктор)
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
            {
                // Используем перемещение из старого вектора
                std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
            }
            else
            {
                // Тип неперемещаем. Используем копирующий конструктор
                std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
            }
            data_.Swap(new_data);
            // Вызываем деструкторы у старых элементов
            std::destroy_n(new_data.GetAddress(), size_);
            ++size_;
        }

        return *(data_.GetAddress() + size_ - 1);
        // Возвращаем ссылку на добавленный элемент, чтобы его можно было сразу использовать
        // cats.EmplaceBack("Tom"s, 5).SayMeow();
    }


    // Метод PushBack должен предоставлять строгую гарантию безопасности исключений, 
    // когда выполняется любое из условий:
    //  -мove-конструктор у типа T объявлен как noexcept;
    //  -тип T имеет публичный конструктор копирования.
    // Если у типа T нет конструктора копирования и move-конструктор может выбрасывать 
    // исключения, метод PushBack должен предоставлять базовую гарантию безопасности исключений.
    // Сложность метода PushBack должна быть амортизированной константой.
    void PushBack(const T& value)
    {
        // Есть ли место по новый элемент?
        if (size_ < Capacity())
        {
            // Место есть. Память пока не распределена.
            new(data_ + size_) T(value);
            ++size_;
        }
        else
        {
            // Места больше нет. Выделяем
            size_t new_capacity = (size_ == 0 ? 1 : size_ * 2);
            RawMemory<T> new_data(new_capacity);
            // Создаем новый элемент в новом буфере
            new (new_data + size_) T(value);
            // Есть ли у типа noexcept-конструктор перемещения ИЛИ отсутствует копирующий конструктор (т.е. есть move-конструктор)
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
            {
                // Используем перемещение из старого вектора
                std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
            }
            else
            {
                // Тип неперемещаем. Используем копирующий конструктор
                std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
            }
            data_.Swap(new_data);
            // Вызываем деструкторы у старых элементов
            std::destroy_n(new_data.GetAddress(), size_);
            ++size_;
        }
    }

    template <typename U>
    void PushBack(U&& value)
    {

        // Есть ли место по новый элемент?
        if (size_ < Capacity())
        {
            // Место есть. Память пока не распределена. Вставляем
            new(data_ + size_) T(std::forward<U>(value));
            ++size_;
        }
        else
        {
            // Места больше нет. Выделяем
            size_t new_capacity = (size_ == 0 ? 1 : size_ * 2);
            RawMemory<T> new_data(new_capacity);
            // Создаем новый элемент в новом буфере
            new (new_data + size_) T(std::forward<U>(value));
            // Копируем или перемещаем старый данные в новый буфер
            // Есть ли у типа noexcept-конструктор перемещения ИЛИ отсутствует копирующий конструктор (т.е. есть move-конструктор)
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
            {
                // Используем перемещение из старого вектора
                std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
            }
            else
            {
                // Тип неперемещаем. Используем копирующий конструктор
                std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
            }
            // Обмениваем буферы
            data_.Swap(new_data);
            // Вызываем деструкторы у старых элементов
            std::destroy_n(new_data.GetAddress(), size_);
            ++size_;
        }
    }

    // Метод PopBack не должен выбрасывать исключений при вызове у непустого вектора. 
    // При вызове PopBack у пустого вектора поведение неопределённо.
    // Метод PopBack должен иметь сложность $O(1)$.
    void PopBack() noexcept
    {
        std::destroy_n(data_.GetAddress() + size_ - 1, 1);
        --size_;
    }


    // Метод Emplace для передачи своих параметров конструктору элемента использует 
    // perfect forwarding. Так как Emplace способен передать свои аргументы любому 
    // конструктору T, включая конструкторы копирования и перемещения, оба метода 
    // Insert можно реализовать на основе Emplace.
    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args)
    {
        // Отдельная обработка вставки в конец
        if (pos == cend())
        {
            // Делаем Emplace_back() с perfect forwarding всех аргументов
            EmplaceBack(std::forward<Args>(args)...);
            // Т.к. EmplaceBack может переаллоцировать память, итератор нужно вычислить
            return (end() - 1);
        }

        // Вычисляем индекс вставляемого элемента в массиве
        // index уже учитивает 0-based индексирование массивов
        size_t index = std::distance(cbegin(), pos);

        // Есть ли место по новый элемент?
        if (size_ < Capacity())
        {
            // Место есть.
            T inserting_value_tmp = T(std::forward<Args>(args)...);
            new(data_ + size_) T(std::move(data_[size_ - 1]));
            std::move_backward(begin() + index, begin() + size_ - 1, begin() + size_);
            data_[index] = std::move(inserting_value_tmp);
            ++size_;
            return begin() + index;
        }
        else
        {
            // Места больше нет. Выделяем
            size_t new_capacity = (size_ == 0 ? 1 : size_ * 2);
            RawMemory<T> new_data(new_capacity);

            // Вставляем новое значение в требуемую позицию нового буфера с отступом, эквивалентном pos старого буфера
            new(new_data.GetAddress() + index) T(std::forward<Args>(args)...);

            // 2. Пытаемся переместить элементы, предшествующие вставляемому элементу.
            //    В случае исключений подчищать нужно вручную.
            try
            {
                // Есть ли у типа noexcept-конструктор перемещения ИЛИ отсутствует копирующий конструктор (т.е. есть move-конструктор)
                if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
                {
                    // Используем перемещение из старого вектора по итераторам
                    std::uninitialized_move(begin(), begin() + index, new_data.GetAddress());
                }
                else
                {
                    // Тип неперемещаем. Используем копирующий конструктор по итераторам
                    std::uninitialized_copy(begin(), begin() + index, new_data.GetAddress());
                }
            }
            catch (...)
            {
                // Перемещение не получилось. Удаляем элемент из шага 1.
                std::destroy_at(new_data.GetAddress() + index);
            }

            // 3. Пытаемся переместить элементы, следующие ЗА вставляемым
            //    В случае исключений подчищать нужно вручную.
            try
            {
                // Есть ли у типа noexcept-конструктор перемещения ИЛИ отсутствует копирующий конструктор (т.е. есть move-конструктор)
                if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
                {
                    // Используем перемещение из старого вектора по итераторам
                    std::uninitialized_move_n(begin() + index, size_ - index, new_data.GetAddress() + index + 1);
                }
                else
                {
                    // Тип неперемещаем. Используем копирующий конструктор по итераторам
                    std::uninitialized_copy_n(begin() + index, size_ - index, new_data.GetAddress() + index + 1);
                }
            }
            catch (...)
            {
                // Перемещение не получилось. Удаляем элементы из шагов 1 и 2.
                std::destroy(new_data.GetAddress(), new_data.GetAddress() + index + 1);
            }

            data_.Swap(new_data);
            std::destroy_n(new_data.GetAddress(), size_);
            ++size_;

            return begin() + index;
        }
    }

    iterator Insert(const_iterator pos, const T& value)
    {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value)
    {
        return Emplace(pos, std::move(value));
    }

    iterator Erase(const_iterator pos)   /*noexcept(std::is_nothrow_move_assignable_v<T>)*/
    {
        // Вычисляем индекс вставляемого элемента в массиве
        size_t index = std::distance(cbegin(), pos);

        std::move(begin() + index + 1, end(), begin() + index);
        PopBack();
        return begin() + index;
    }

    size_t Size() const noexcept
    {
        return size_;
    }

    size_t Capacity() const noexcept
    {
        return data_.Capacity();
    }

    /*
    В константном операторе [] используется оператор const_cast, чтобы снять 
    константность с ссылки на текущий объект и вызвать неконстантную версию 
    оператора []. Так получится избавиться от дублирования проверки assert(index < size).
    */
    const T& operator[](size_t index) const noexcept
    {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept
    {
        assert(index < size_);
        return data_[index];
    }

    void Swap(Vector& other) noexcept
    {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }


    void Reserve(size_t new_capacity)
    {
        if (new_capacity <= data_.Capacity())
        {
            return;
        }
        RawMemory<T> new_data(new_capacity);

        // Есть ли у типа noexcept-конструктор перемещения ИЛИ отсутствует копирующий конструктор (т.е. есть move-конструктор)
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
        {
            // Используем перемещение из старого вектора
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        else
        {
            // Тип неперемещаем. Используем копирующий конструктор
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }

        // Удаляем элементы старого вектора
        std::destroy_n(data_.GetAddress(), size_);
        // Обмениваемся указателями на буфер
        data_.Swap(new_data);
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    // Вызывает деструкторы n объектов массива по адресу buf
    static void DestroyN(T* buf, size_t n) noexcept
    {
        for (size_t i = 0; i != n; ++i)
        {
            Destroy(buf + i);
        }
    }

    // Создаёт копию объекта elem в сырой памяти по адресу buf
    static void CopyConstruct(T* buf, const T& elem)
    {
        new (buf) T(elem);
    }

    // Вызывает деструктор объекта по адресу buf
    static void Destroy(T* buf) noexcept
    {
        buf->~T();
    }
};
