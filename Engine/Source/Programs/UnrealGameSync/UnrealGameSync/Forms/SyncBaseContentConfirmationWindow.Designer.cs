// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealGameSync.Forms
{
	partial class SyncBaseContentConfirmationWindow
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
			flowLayoutPanel1 = new System.Windows.Forms.FlowLayoutPanel();
			label1 = new System.Windows.Forms.Label();
			BaseContentPaths = new System.Windows.Forms.Label();
			label2 = new System.Windows.Forms.Label();
			label3 = new System.Windows.Forms.Label();
			SyncBaseContentButton = new System.Windows.Forms.Button();
			DoNotSyncBaseContentButton = new System.Windows.Forms.Button();
			tableLayoutPanel1 = new System.Windows.Forms.TableLayoutPanel();
			flowLayoutPanel2 = new System.Windows.Forms.FlowLayoutPanel();
			flowLayoutPanel1.SuspendLayout();
			tableLayoutPanel1.SuspendLayout();
			flowLayoutPanel2.SuspendLayout();
			SuspendLayout();
			// 
			// flowLayoutPanel1
			// 
			flowLayoutPanel1.AutoSize = true;
			flowLayoutPanel1.Controls.Add(label1);
			flowLayoutPanel1.Controls.Add(BaseContentPaths);
			flowLayoutPanel1.Controls.Add(label2);
			flowLayoutPanel1.Controls.Add(label3);
			flowLayoutPanel1.Dock = System.Windows.Forms.DockStyle.Fill;
			flowLayoutPanel1.FlowDirection = System.Windows.Forms.FlowDirection.TopDown;
			flowLayoutPanel1.Location = new System.Drawing.Point(3, 2);
			flowLayoutPanel1.Margin = new System.Windows.Forms.Padding(3, 2, 3, 2);
			flowLayoutPanel1.Name = "flowLayoutPanel1";
			flowLayoutPanel1.Padding = new System.Windows.Forms.Padding(4);
			flowLayoutPanel1.Size = new System.Drawing.Size(522, 83);
			flowLayoutPanel1.TabIndex = 0;
			// 
			// label1
			// 
			label1.AutoSize = true;
			label1.Location = new System.Drawing.Point(7, 4);
			label1.Name = "label1";
			label1.Size = new System.Drawing.Size(485, 15);
			label1.TabIndex = 0;
			label1.Text = "Activating \"Sync Base Content\" cannot be undone but will allow you to sync content from:";
			// 
			// BaseContentPaths
			// 
			BaseContentPaths.AutoSize = true;
			BaseContentPaths.Location = new System.Drawing.Point(7, 19);
			BaseContentPaths.Name = "BaseContentPaths";
			BaseContentPaths.Padding = new System.Windows.Forms.Padding(15, 0, 0, 15);
			BaseContentPaths.Size = new System.Drawing.Size(86, 30);
			BaseContentPaths.TabIndex = 3;
			BaseContentPaths.Text = "[Base Paths]";
			// 
			// label2
			// 
			label2.AutoSize = true;
			label2.Location = new System.Drawing.Point(7, 49);
			label2.Name = "label2";
			label2.Size = new System.Drawing.Size(448, 15);
			label2.TabIndex = 1;
			label2.Text = "If you want to get back to the current state you will have to create a new workspace.";
			// 
			// label3
			// 
			label3.AutoSize = true;
			label3.Location = new System.Drawing.Point(7, 64);
			label3.Name = "label3";
			label3.Size = new System.Drawing.Size(137, 15);
			label3.TabIndex = 2;
			label3.Text = "Do you wish to proceed?";
			// 
			// SyncBaseContentButton
			// 
			SyncBaseContentButton.DialogResult = System.Windows.Forms.DialogResult.OK;
			SyncBaseContentButton.Location = new System.Drawing.Point(421, 6);
			SyncBaseContentButton.Margin = new System.Windows.Forms.Padding(3, 2, 3, 2);
			SyncBaseContentButton.Name = "SyncBaseContentButton";
			SyncBaseContentButton.Size = new System.Drawing.Size(90, 28);
			SyncBaseContentButton.TabIndex = 0;
			SyncBaseContentButton.Text = "Ok";
			SyncBaseContentButton.UseVisualStyleBackColor = true;
			// 
			// DoNotSyncBaseContentButton
			// 
			DoNotSyncBaseContentButton.DialogResult = System.Windows.Forms.DialogResult.Cancel;
			DoNotSyncBaseContentButton.Location = new System.Drawing.Point(325, 6);
			DoNotSyncBaseContentButton.Margin = new System.Windows.Forms.Padding(3, 2, 3, 2);
			DoNotSyncBaseContentButton.Name = "DoNotSyncBaseContentButton";
			DoNotSyncBaseContentButton.Size = new System.Drawing.Size(90, 28);
			DoNotSyncBaseContentButton.TabIndex = 1;
			DoNotSyncBaseContentButton.Text = "Cancel";
			DoNotSyncBaseContentButton.UseVisualStyleBackColor = true;
			// 
			// tableLayoutPanel1
			// 
			tableLayoutPanel1.AutoSize = true;
			tableLayoutPanel1.AutoSizeMode = System.Windows.Forms.AutoSizeMode.GrowAndShrink;
			tableLayoutPanel1.ColumnCount = 1;
			tableLayoutPanel1.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
			tableLayoutPanel1.Controls.Add(flowLayoutPanel1, 0, 0);
			tableLayoutPanel1.Controls.Add(flowLayoutPanel2, 0, 1);
			tableLayoutPanel1.Dock = System.Windows.Forms.DockStyle.Fill;
			tableLayoutPanel1.Location = new System.Drawing.Point(0, 0);
			tableLayoutPanel1.Margin = new System.Windows.Forms.Padding(3, 2, 3, 2);
			tableLayoutPanel1.Name = "tableLayoutPanel1";
			tableLayoutPanel1.RowCount = 2;
			tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle());
			tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Absolute, 60F));
			tableLayoutPanel1.Size = new System.Drawing.Size(528, 95);
			tableLayoutPanel1.TabIndex = 1;
			// 
			// flowLayoutPanel2
			// 
			flowLayoutPanel2.AutoSize = true;
			flowLayoutPanel2.AutoSizeMode = System.Windows.Forms.AutoSizeMode.GrowAndShrink;
			flowLayoutPanel2.Controls.Add(SyncBaseContentButton);
			flowLayoutPanel2.Controls.Add(DoNotSyncBaseContentButton);
			flowLayoutPanel2.Dock = System.Windows.Forms.DockStyle.Fill;
			flowLayoutPanel2.FlowDirection = System.Windows.Forms.FlowDirection.RightToLeft;
			flowLayoutPanel2.Location = new System.Drawing.Point(3, 89);
			flowLayoutPanel2.Margin = new System.Windows.Forms.Padding(3, 2, 3, 2);
			flowLayoutPanel2.Name = "flowLayoutPanel2";
			flowLayoutPanel2.Padding = new System.Windows.Forms.Padding(4);
			flowLayoutPanel2.Size = new System.Drawing.Size(522, 56);
			flowLayoutPanel2.TabIndex = 1;
			// 
			// SyncBaseContentConfirmationWindow
			// 
			AutoScaleMode = System.Windows.Forms.AutoScaleMode.Dpi;
			this.AutoScaleDimensions = new System.Drawing.SizeF(96F, 96F);
			ClientSize = new System.Drawing.Size(620, 180);
			Controls.Add(tableLayoutPanel1);
			FormBorderStyle = System.Windows.Forms.FormBorderStyle.Sizable;
			Margin = new System.Windows.Forms.Padding(3, 2, 3, 2);
			MinimumSize = new System.Drawing.Size(400, 180);
			Name = "SyncBaseContentConfirmationWindow";
			Text = "Warning";
			flowLayoutPanel1.ResumeLayout(false);
			flowLayoutPanel1.PerformLayout();
			tableLayoutPanel1.ResumeLayout(false);
			tableLayoutPanel1.PerformLayout();
			flowLayoutPanel2.ResumeLayout(false);
			ResumeLayout(false);
			PerformLayout();
		}

		#endregion
		private System.Windows.Forms.FlowLayoutPanel flowLayoutPanel1;
		private System.Windows.Forms.Label label1;
		private System.Windows.Forms.Label label2;
		private System.Windows.Forms.Label label3;
		private System.Windows.Forms.Button SyncBaseContentButton;
		private System.Windows.Forms.Button DoNotSyncBaseContentButton;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel1;
		private System.Windows.Forms.FlowLayoutPanel flowLayoutPanel2;
		private System.Windows.Forms.Label BaseContentPaths;
	}
}