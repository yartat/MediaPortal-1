namespace MPTvClient
{
    partial class frmServerConnectionConfig
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(frmServerConnectionConfig));
            this.label1 = new System.Windows.Forms.Label();
            this.edHostname = new System.Windows.Forms.TextBox();
            this.labelConnectionKey = new System.Windows.Forms.Label();
            this.edConnectionKey = new System.Windows.Forms.TextBox();
            this.btnCancel = new System.Windows.Forms.Button();
            this.btnOk = new System.Windows.Forms.Button();
            this.SuspendLayout();
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(20, 37);
            this.label1.Margin = new System.Windows.Forms.Padding(4, 0, 4, 0);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(87, 20);
            this.label1.TabIndex = 0;
            this.label1.Text = "Hostname:";
            // 
            // edHostname
            // 
            this.edHostname.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.edHostname.Location = new System.Drawing.Point(116, 32);
            this.edHostname.Margin = new System.Windows.Forms.Padding(4, 5, 4, 5);
            this.edHostname.Name = "edHostname";
            this.edHostname.Size = new System.Drawing.Size(264, 26);
            this.edHostname.TabIndex = 1;
            //
            // labelConnectionKey
            //
            this.labelConnectionKey.AutoSize = true;
            this.labelConnectionKey.Location = new System.Drawing.Point(20, 85);
            this.labelConnectionKey.Margin = new System.Windows.Forms.Padding(4, 0, 4, 0);
            this.labelConnectionKey.Name = "labelConnectionKey";
            this.labelConnectionKey.Size = new System.Drawing.Size(120, 20);
            this.labelConnectionKey.TabIndex = 2;
            this.labelConnectionKey.Text = "Connection key:";
            //
            // edConnectionKey
            //
            this.edConnectionKey.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left)
            | System.Windows.Forms.AnchorStyles.Right)));
            this.edConnectionKey.Location = new System.Drawing.Point(148, 80);
            this.edConnectionKey.Margin = new System.Windows.Forms.Padding(4, 5, 4, 5);
            this.edConnectionKey.Name = "edConnectionKey";
            this.edConnectionKey.Size = new System.Drawing.Size(232, 26);
            this.edConnectionKey.TabIndex = 3;
            //
            // btnCancel
            //
            this.btnCancel.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.btnCancel.DialogResult = System.Windows.Forms.DialogResult.Cancel;
            this.btnCancel.Location = new System.Drawing.Point(244, 140);
            this.btnCancel.Margin = new System.Windows.Forms.Padding(4, 5, 4, 5);
            this.btnCancel.Name = "btnCancel";
            this.btnCancel.Size = new System.Drawing.Size(112, 35);
            this.btnCancel.TabIndex = 9;
            this.btnCancel.Text = "Cancel";
            this.btnCancel.UseVisualStyleBackColor = true;
            // 
            // btnOk
            // 
            this.btnOk.DialogResult = System.Windows.Forms.DialogResult.OK;
            this.btnOk.Location = new System.Drawing.Point(34, 140);
            this.btnOk.Margin = new System.Windows.Forms.Padding(4, 5, 4, 5);
            this.btnOk.Name = "btnOk";
            this.btnOk.Size = new System.Drawing.Size(112, 35);
            this.btnOk.TabIndex = 8;
            this.btnOk.Text = "Ok";
            this.btnOk.UseVisualStyleBackColor = true;
            // 
            // frmServerConnectionConfig
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(9F, 20F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(399, 210);
            this.Controls.Add(this.edConnectionKey);
            this.Controls.Add(this.labelConnectionKey);
            this.Controls.Add(this.btnCancel);
            this.Controls.Add(this.btnOk);
            this.Controls.Add(this.edHostname);
            this.Controls.Add(this.label1);
            this.Icon = ((System.Drawing.Icon)(resources.GetObject("$this.Icon")));
            this.Margin = new System.Windows.Forms.Padding(4, 5, 4, 5);
            this.Name = "frmServerConnectionConfig";
            this.StartPosition = System.Windows.Forms.FormStartPosition.CenterScreen;
            this.Text = "Server connection config";
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.TextBox edHostname;
        private System.Windows.Forms.Label labelConnectionKey;
        private System.Windows.Forms.TextBox edConnectionKey;
        private System.Windows.Forms.Button btnCancel;
        private System.Windows.Forms.Button btnOk;
    }
}