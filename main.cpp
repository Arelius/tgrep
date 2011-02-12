#include <cstdio>
#include <cstring>
#include <assert.h>

// These are BIG log files! Thankfully you remembered to compile me 64-bit right?

FILE* file;
// Small for testing purposes, enlarge for speed.
// In all, the ideal size is going to be big enough so that on average we catch
// a date in a single fseek, but not much more. The performance is highly data
// dependant.
const size_t buffer_size = 10;
char buffer[buffer_size*2+1];
size_t read_len;
char* curr;
off_t buffer_start;

int start_day = 0;

// In retrospect I should probally just invert all of these for clarity, at the
// cost of typing a bit more.
typedef bool ebool;

void fill_buffer()
{
    buffer_start = ftello(file);
    read_len = fread(buffer, 1, buffer_size*2, file);
    buffer[read_len] = '\0';
    curr = buffer;
}

void inc_buffer()
{
    assert(read_len > buffer_size);
    read_len -= buffer_size;
    curr -= buffer_size;
    buffer_start += buffer_size;

    if(read_len < buffer_size)
    {
        assert(feof(file));
        return;
    }

    assert(read_len == buffer_size);
    memcpy(buffer, buffer+buffer_size, read_len);

    read_len += fread(buffer+buffer_size, 1, buffer_size, file);
    buffer[read_len] = '\0';

}

ebool inc_curr()
{
    curr++;
    if((curr - buffer) > buffer_size)
        inc_buffer();
    return feof(file);
}

bool is_whitespace(char c)
{
    return (c == ' ' ||
            c == '\t');
}

ebool consume_whitespace()
{
    while(is_whitespace(*curr) && !inc_curr());
    return feof(file);
}

const char* month_codes[] =
{
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Oct",
    "Nov",
    "Dec"
};

unsigned int num_months = 12;

int parse_month(const char* str)
{
    for(unsigned int i=0; i< num_months; i++)
    {
        if(strncmp(str, month_codes[i], 3) == 0)
            return i+1;
    }
    return -1;
}

bool is_num(char c)
{
    return (c >= '0' && c <= '9');
}

int parse_int()
{
    size_t max_int_size = 2; // Big enough for days, hours, mins, and seconds
    char int_buffer[max_int_size + 1];
    int i=0;

    while(i<max_int_size)
    {
        if(!is_num(*curr))
        {
            if(i == 0)
                return -1;
            else break;
        }
        int_buffer[i] = *curr;
        if(inc_curr())
            return -1;
        i++;
    }

    int_buffer[i] = '\0';

    return atoi(int_buffer);
}

ebool parse_char(char c)
{
    if(*curr == c)
        return inc_curr();
    else
        return false;
}

int find_next_date(off_t* out_offset = NULL, int* out_day = NULL)
{
    // Let's use pretty strict date parsing, don't want to run into something else by accident.
    // Could check for beginning of line, But really it's your own goddamn fault if your logs
    // have extra dates.

    while(!feof(file))
    {
        if(parse_month(curr) > 0)
        {
            off_t start_offset = buffer_start +
                (curr-buffer);

            if(inc_curr() || inc_curr() || inc_curr()) // Bleh!
                return -1;

            if(consume_whitespace())
                return -1;

            int day = parse_int();
            if(day == -1 || consume_whitespace())
                return -1;

            int hour = parse_int();
            if(hour == -1 || parse_char(':'))
                return -1;

            int minute = parse_int();
            if(minute == -1 || parse_char(':'))
                return -1;

            int second = parse_int();
            if(second == -1)
                return -1;

            int seconds = (hour * 60 * 60) +
                          (minute * 60) +
                          (second);

            // Be smarter about this, probally shouldn't assume that every other day is just the next, but it works for our needs.
            if(day != start_day)
                seconds += 24 * 60 * 60;

            if(out_offset)
                *out_offset = start_offset;

            if(out_day)
                *out_day = day;

            return seconds;
        }
        inc_curr();

    }
    return -1;
}

int start_time;
int end_time;

off_t start_offset;
off_t end_offset;
off_t semi_end;

off_t file_len;

// Size of a range where we can't have more then a single date.
const size_t termination_range = 14;

enum bisect_mask
{
    SEARCH_START,
    SEARCH_END,
    SEARCH_BOTH
};

void bisect_range(off_t start, off_t end, bisect_mask mask = SEARCH_BOTH)
{
    // Probally get a bit better if we offset this a bit back but w/e
    size_t range = (end - start);
    off_t center = start + range/2;

    //printf("Bisect %d %d-%d\n", (int)range, (int)start, (int)end);

    if(range <= termination_range)
    {
        return;
    }

    fseeko(file, center, SEEK_SET);
    fill_buffer();

    off_t date_offset;
    int time = find_next_date(&date_offset);

    if(time == -1 || date_offset >= end)
    {
        // Ugh, just scanned the second half and found no dates!
        bisect_range(start, center, mask);
        return;
    }


    // Shouldn't actually need the mask test here.

    if((mask == SEARCH_START || mask == SEARCH_BOTH) &&
       time < start_time && date_offset > start_offset)
        start_offset = date_offset;

    if((mask == SEARCH_END || mask == SEARCH_BOTH) &&
       time > end_time && date_offset < end_offset)
        semi_end = end_offset = date_offset;

    assert(end_offset != start_offset);

    // This should be sufficient to ensure that all but one recursive call
    // should be tail call optimizable, given a decent optimizer.

    // TODO: Clean this up!

    if(mask == SEARCH_START)
    {
        if(time >= start_time)
            bisect_range(start, date_offset, mask);
        else
            bisect_range(date_offset, end, mask);
    }
    else if(mask == SEARCH_END)
    {
        if(time > end_time)
            bisect_range(start, date_offset, mask);
        else
            bisect_range(date_offset, end, mask);
        
    }
    else
    {
        if(time >= start_time && time <= end_time)
        {
            // We've just bisected the sequence, split calls.
            bisect_range(start, date_offset, SEARCH_START);
            bisect_range(date_offset, end), SEARCH_END;
        }
        else if (time < start)
        {
            bisect_range(date_offset, end, mask);
        }
        else //if (time > end)
        {
            bisect_range(start, date_offset, mask);
        }
    }
}

// In case we get the same time again, we will treat the timestamp as the first
// logical instance of that time, this gives us more useful results then trying
// both times and mixing up our results, It's unlikely that total number will be
// useful if we start counting two days. So: when we read in the time, normalize
// it to the first instance of time, the second time should always be after that.

int main(int argc, char** argv)
{
    const char* logfile = "data/small";
    file  = fopen(logfile, "rb");

    // Should be 10 dates in this range.
    start_time  = (10 * 60 * 60) + (52 * 60);
    end_time = (20 * 60 * 60) + (7 * 60);

    fill_buffer();
    find_next_date(NULL, &start_day);

    fseek(file, 0, SEEK_END);
    file_len = ftello(file);

    start_offset = 0;
    end_offset = file_len;

    bisect_range(start_offset, end_offset);
    
    // We have the last date prior to our range, skip to the next date on the start.
    fseeko(file, start_offset, SEEK_SET);
    fgetc(file); // So we can move past this date;
    fill_buffer();
    
    int date = find_next_date(&start_offset);

    // Verify date. if invalid we have some weird condition. Error.

    // Fixme! File may be very big, don't want to load the entire range in memory!
    char* printbuf = (char*)malloc(end_offset-start_offset+1);
    fseeko(file, start_offset, SEEK_SET);
    fread(printbuf, 1, end_offset-start_offset, file);

    printbuf[end_offset-start_offset] = '\0';
    puts(printbuf);
    free(printbuf);
    

    fclose(file);
}
