package main

import (
	"bytes"
	"errors"
	"fmt"
	"github.com/pterm/pterm"
	"io"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"sync"
)

const (
	DlGoroutines int    = 4 // goroutines number for parallel downloading.
	DlRoot       string = "dl/"
)

func main() {
	htmlBytes, err := ioutil.ReadFile("index.html")
	if err != nil {
		log.Fatal(err)
	}

	// parsing html file to get matrix metadata
	if matMates, err := sourceParse(bytes.NewReader(htmlBytes)); err != nil {
		log.Fatal(err)
		return
	} else {
		fmt.Printf("Downloading %d matrices using %d go goroutines", len(matMates), DlGoroutines)

		// set terminal processing bar
		pterm.DefaultSection.Println("Download Matrices")
		processbar, err := pterm.DefaultProgressbar.WithTotal(len(matMates)).WithTitle("Downloading matrices").Start()
		if err != nil {
			log.Fatal(err)
			return
		}

		var wg sync.WaitGroup
		var terminalLock = &sync.RWMutex{}
		dlTasks := make(chan bool, DlGoroutines)
		for _, mat := range matMates {
			wg.Add(1)
			dlTasks <- true
			_mat := mat
			go func() {
				dl(_mat, processbar, terminalLock)
				wg.Done()
				<-dlTasks
			}()
		}
		wg.Wait()
		if _, err := processbar.Stop(); err != nil {
			return
		}
	}
}

func dl(mat MatrixMeta, processbar *pterm.ProgressbarPrinter, terminalLock *sync.RWMutex) {
	terminalLock.Lock()
	processbar.Title = "Downloading " + mat.Name
	terminalLock.Unlock()

	if err := downloadFile(DlRoot+mat.Name+".tar.gz", mat.DlLinks.MatrixMarket); err != nil {
		pterm.Error.Printf(err.Error())
		// todo: stop the whole downloading task if it has error
		return
	}

	terminalLock.Lock()
	pterm.Success.Println("Downloaded matrix", mat.Name)
	processbar.Increment()
	terminalLock.Unlock()
}

func downloadFile(filepath string, url string) (err error) {
	if _, err := os.Stat(filepath); err == nil {
		return fmt.Errorf("file %s exists", filepath)
	} else if errors.Is(err, os.ErrNotExist) {
		// fall through
	} else {
		return err
	}

	// Create the tar.gz file
	out, err := os.Create(filepath)
	if err != nil {
		return err
	}
	defer out.Close()

	// Get the data
	client := http.Client{} // {Timeout: 10 * time.Second}
	resp, err := client.Get(url)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	// Check server response
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("bad status: %s", resp.Status)
	}

	// Writer the body to file
	_, err = io.Copy(out, resp.Body)
	if err != nil {
		return err
	}

	return nil
}
